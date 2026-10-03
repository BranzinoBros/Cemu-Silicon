#include "AArch64CodeArena.h"

#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#if BOOST_OS_MACOS
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif

namespace AArch64CodeArena
{
	// address space to reserve, the smaller sizes are only tried if the larger reservation fails
	static constexpr size_t RESERVE_SIZES[] = {512 * 1024 * 1024, 256 * 1024 * 1024, 128 * 1024 * 1024};
	static constexpr size_t CODE_ALIGNMENT = 16;

	static std::mutex s_mutex;
	static bool s_initialized = false;
	static uint8* s_base = nullptr;
	static size_t s_capacity = 0;
	static size_t s_used = 0;
	static bool s_exhaustedLogged = false;

	static void Reserve()
	{
		s_initialized = true;
		int flags = MAP_PRIVATE | MAP_ANON;
#if BOOST_OS_MACOS
		flags |= MAP_JIT;
#endif
		for (size_t reserveSize : RESERVE_SIZES)
		{
			void* p = mmap(nullptr, reserveSize, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
			if (p == MAP_FAILED)
				continue;
			s_base = static_cast<uint8*>(p);
			s_capacity = reserveSize;
			cemuLog_log(LogType::Force, "Recompiler: Reserved {}MB of address space for AArch64 code", reserveSize / 1024 / 1024);
			return;
		}
		cemuLog_log(LogType::Force, "Recompiler: Failed to reserve executable memory for AArch64 code (errno {})", errno);
	}

	void* Commit(const void* code, size_t size)
	{
		cemu_assert_debug(size > 0 && (size % 4) == 0);
		uint8* dst;
		{
			std::lock_guard lock(s_mutex);
			if (!s_initialized)
				Reserve();
			if (!s_base)
				return nullptr;
			size_t offset = (s_used + CODE_ALIGNMENT - 1) & ~(CODE_ALIGNMENT - 1);
			if (offset > s_capacity || size > s_capacity - offset)
			{
				if (!s_exhaustedLogged)
				{
					s_exhaustedLogged = true;
					cemuLog_log(LogType::Force, "Recompiler: AArch64 code arena is full ({}MB used). Newly recompiled functions will run in the interpreter", s_used / 1024 / 1024);
				}
				return nullptr;
			}
			s_used = offset + size;
			dst = s_base + offset;
		}
		// the reserved range [dst, dst+size) belongs to this caller now, the copy happens outside of the lock
#if BOOST_OS_MACOS
		pthread_jit_write_protect_np(0);
		std::memcpy(dst, code, size);
		pthread_jit_write_protect_np(1);
		sys_icache_invalidate(dst, size);
#else
		std::memcpy(dst, code, size);
		__builtin___clear_cache(reinterpret_cast<char*>(dst), reinterpret_cast<char*>(dst + size));
#endif
		return dst;
	}
}; // namespace AArch64CodeArena
