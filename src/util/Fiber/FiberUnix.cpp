#include "Fiber.h"
#include <ucontext.h>
#include <atomic>

thread_local Fiber* sCurrentFiber{};

namespace
{
	struct FiberUnixContext
	{
		ucontext_t ctx;
		void(*entryPoint)(void* userParam);
		void* userParam;
	};

	// makecontext only passes int-sized arguments portably, so the context pointer is split into two 32-bit halves
	void FiberEntryTrampoline(uint32 contextHigh, uint32 contextLow)
	{
		FiberUnixContext* fiberCtx = (FiberUnixContext*)(((uint64)contextHigh << 32) | (uint64)contextLow);
		fiberCtx->entryPoint(fiberCtx->userParam);
	}
}

Fiber::Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData) : m_privateData(privateData)
{
	FiberUnixContext* fiberCtx = (FiberUnixContext*)malloc(sizeof(FiberUnixContext));
	fiberCtx->entryPoint = FiberEntryPoint;
	fiberCtx->userParam = userParam;
	ucontext_t* ctx = &fiberCtx->ctx;
	
	const size_t stackSize = 2 * 1024 * 1024;
	m_stackPtr = malloc(stackSize);

	getcontext(ctx);
	ctx->uc_stack.ss_sp = m_stackPtr;
	ctx->uc_stack.ss_size = stackSize;
	ctx->uc_link = &ctx[0];
	// https://www.man7.org/linux/man-pages/man3/makecontext.3.html#NOTES
	makecontext(ctx, (void(*)())FiberEntryTrampoline, 2, (uint32)((uint64)fiberCtx >> 32), (uint32)(uint64)fiberCtx);
	this->m_implData = (void*)fiberCtx;
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
	FiberUnixContext* fiberCtx = (FiberUnixContext*)malloc(sizeof(FiberUnixContext));
	fiberCtx->entryPoint = nullptr;
	fiberCtx->userParam = nullptr;
	getcontext(&fiberCtx->ctx);
	this->m_implData = (void*)fiberCtx;
	m_stackPtr = nullptr;
}

Fiber::~Fiber()
{
	if(m_stackPtr)
		free(m_stackPtr);
	free(m_implData);
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	cemu_assert_debug(sCurrentFiber == nullptr);
    sCurrentFiber = new Fiber(privateData);
	return sCurrentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
    Fiber* leavingFiber = sCurrentFiber;
    sCurrentFiber = &targetFiber;
	std::atomic_thread_fence(std::memory_order_seq_cst);
	swapcontext(&((FiberUnixContext*)leavingFiber->m_implData)->ctx, &((FiberUnixContext*)targetFiber.m_implData)->ctx);
	std::atomic_thread_fence(std::memory_order_seq_cst);
}

void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}
