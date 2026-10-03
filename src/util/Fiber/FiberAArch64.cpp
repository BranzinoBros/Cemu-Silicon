// Userspace fiber implementation for AArch64 (Apple Silicon)
//
// swapcontext() on macOS saves and restores the signal mask with a sigprocmask syscall on every call.
// Guest threads are fibers and switch very often, so this implementation switches stacks in userspace
// and only saves what AAPCS64 requires a callee to preserve: x19-x28, fp (x29), lr (x30), sp and d8-d15.
// x18 is reserved for the platform on Apple arm64 and is never touched.
//
// FPCR is deliberately not part of the fiber context. Every host thread that runs fibers (OSSched[core=N])
// sets flush-to-zero once and so does every guest thread fiber on entry, nothing else in Cemu writes FPCR,
// so the value is identical on all host threads that a fiber can run on, even if a guest thread migrates
// between host threads after an affinity change.

#include "Fiber.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(__aarch64__)
#error "FiberAArch64.cpp requires an AArch64 target"
#endif
#if !defined(__clang__)
#error "FiberAArch64.cpp requires Clang (naked function support on AArch64)"
#endif

thread_local Fiber* sCurrentFiber{};

namespace
{
	constexpr size_t FIBER_STACK_SIZE = 2 * 1024 * 1024;

	// layout of the register save area at the saved stack pointer of a suspended fiber
	// x19-x28 at [0, 80), x29 at 80, x30 at 88, d8-d15 at [96, 160)
	constexpr size_t FRAME_SIZE = 160;
	constexpr size_t FRAME_SLOT_X19 = 0;
	constexpr size_t FRAME_SLOT_X20 = 1;
	constexpr size_t FRAME_SLOT_X21 = 2;
	constexpr size_t FRAME_SLOT_X29 = 10;
	constexpr size_t FRAME_SLOT_X30 = 11;
	static_assert(FRAME_SIZE % 16 == 0);

	// saves the callee-saved registers of the current context on its stack and stores the resulting stack pointer in *saveStackPtr
	// then loads the context saved at loadStackPtr and returns into it
	__attribute__((naked, noinline)) void CemuFiberSwitchContext(void** saveStackPtr, void* loadStackPtr)
	{
		asm volatile(
			"sub sp, sp, #160\n"
			"stp x19, x20, [sp, #0]\n"
			"stp x21, x22, [sp, #16]\n"
			"stp x23, x24, [sp, #32]\n"
			"stp x25, x26, [sp, #48]\n"
			"stp x27, x28, [sp, #64]\n"
			"stp x29, x30, [sp, #80]\n"
			"stp d8, d9, [sp, #96]\n"
			"stp d10, d11, [sp, #112]\n"
			"stp d12, d13, [sp, #128]\n"
			"stp d14, d15, [sp, #144]\n"
			"mov x9, sp\n"
			"str x9, [x0]\n"
			"mov sp, x1\n"
			"ldp x19, x20, [sp, #0]\n"
			"ldp x21, x22, [sp, #16]\n"
			"ldp x23, x24, [sp, #32]\n"
			"ldp x25, x26, [sp, #48]\n"
			"ldp x27, x28, [sp, #64]\n"
			"ldp x29, x30, [sp, #80]\n"
			"ldp d8, d9, [sp, #96]\n"
			"ldp d10, d11, [sp, #112]\n"
			"ldp d12, d13, [sp, #128]\n"
			"ldp d14, d15, [sp, #144]\n"
			"add sp, sp, #160\n"
			"ret\n"
		);
	}

	// first code executed by a new fiber, entered via the ret of CemuFiberSwitchContext
	// x19 = entry point, x20 = user parameter, x21 = handler called if the entry point ever returns
	// the frame pointer is cleared so that frame-pointer based stack walks terminate here
	__attribute__((naked, noinline)) void CemuFiberEntryTrampoline()
	{
		asm volatile(
			"mov x29, xzr\n"
			"mov x0, x20\n"
			"blr x19\n"
			"blr x21\n"
			"brk #0xf1\n"
		);
	}

	[[noreturn]] void FiberEntryReturned()
	{
		fprintf(stderr, "Fiber entry point returned\n");
		abort();
	}

	size_t GetHostPageSize()
	{
		static const size_t s_pageSize = (size_t)sysconf(_SC_PAGESIZE); // 16KB on Apple Silicon
		return s_pageSize;
	}

	size_t GetStackSize()
	{
		const size_t pageSize = GetHostPageSize();
		return (FIBER_STACK_SIZE + pageSize - 1) & ~(pageSize - 1);
	}

	// one inaccessible guard page below the stack so that an overflow faults instead of corrupting other memory
	size_t GetStackMappingSize()
	{
		return GetHostPageSize() + GetStackSize();
	}
}

Fiber::Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData) : m_privateData(privateData)
{
	const size_t pageSize = GetHostPageSize();
	const size_t mappingSize = GetStackMappingSize();
	void* mapping = mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
	{
		fprintf(stderr, "Fiber: Failed to allocate fiber stack\n");
		abort();
	}
	if (mprotect(mapping, pageSize, PROT_NONE) != 0)
	{
		fprintf(stderr, "Fiber: Failed to protect fiber stack guard page\n");
		abort();
	}
	m_stackPtr = mapping;

	// build an initial register save area at the top of the stack which CemuFiberSwitchContext will "return" into
	uintptr_t stackTop = (uintptr_t)mapping + mappingSize; // page aligned and thus 16-byte aligned as required by AAPCS64
	uint64* frame = (uint64*)(stackTop - FRAME_SIZE); // anonymous mappings are zero-filled, unused slots stay zero
	frame[FRAME_SLOT_X19] = (uint64)(uintptr_t)FiberEntryPoint;
	frame[FRAME_SLOT_X20] = (uint64)(uintptr_t)userParam;
	frame[FRAME_SLOT_X21] = (uint64)(uintptr_t)&FiberEntryReturned;
	frame[FRAME_SLOT_X29] = 0;
	frame[FRAME_SLOT_X30] = (uint64)(uintptr_t)&CemuFiberEntryTrampoline;
	m_implData = (void*)frame;
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
	// the stack pointer is stored in m_implData when switching away from this fiber for the first time
	m_implData = nullptr;
	m_stackPtr = nullptr;
}

Fiber::~Fiber()
{
	if (m_stackPtr)
		munmap(m_stackPtr, GetStackMappingSize());
}

// The functions accessing sCurrentFiber are kept out of line. A fiber can be resumed on a different host thread
// and with LTO an inlined access could reuse a thread-local address computed before the switch.

__attribute__((noinline)) Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	cemu_assert_debug(sCurrentFiber == nullptr);
	sCurrentFiber = new Fiber(privateData);
	return sCurrentFiber;
}

__attribute__((noinline)) void Fiber::Switch(Fiber& targetFiber)
{
	Fiber* leavingFiber = sCurrentFiber;
	sCurrentFiber = &targetFiber;
	std::atomic_thread_fence(std::memory_order_seq_cst);
	CemuFiberSwitchContext(&leavingFiber->m_implData, targetFiber.m_implData);
	std::atomic_thread_fence(std::memory_order_seq_cst);
}

__attribute__((noinline)) void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}
