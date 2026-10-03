#include "util/MemMapper/MemMapper.h"

#include <unistd.h>
#include <sys/mman.h>

namespace MemMapper
{
	const size_t sPageSize{ []()
		{
		return (size_t)getpagesize();
	}()
	};

	size_t GetPageSize()
	{
		return sPageSize;
	}

	int GetProt(PAGE_PERMISSION permissionFlags)
	{
		int  p = 0;
		if (HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_READ) && HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_WRITE) && HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_EXECUTE))
			p = PROT_READ | PROT_WRITE | PROT_EXEC;
		else if (HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_READ) && HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_WRITE) && !HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_EXECUTE))
			p = PROT_READ | PROT_WRITE;
		else if (HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_READ) && !HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_WRITE) && !HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_EXECUTE))
			p = PROT_READ;
		else
			cemu_assert_unimplemented();
		return p;
	}

	void* ReserveMemory(void* baseAddr, size_t size, PAGE_PERMISSION permissionFlags)
	{
		return mmap(baseAddr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}

	void FreeReservation(void* baseAddr, size_t size)
	{
		munmap(baseAddr, size);
	}

	// mprotect() requires page aligned addresses. The host page size (16KiB on Apple Silicon) can be larger than the 4KiB granularity used by callers
	// commit all host pages which overlap with the requested range
	void* AllocateMemory(void* baseAddr, size_t size, PAGE_PERMISSION permissionFlags, bool fromReservation)
	{
		void* r;
		if(fromReservation)
		{
			const uintptr_t alignedBegin = (uintptr_t)baseAddr & ~(uintptr_t)(sPageSize - 1);
			const uintptr_t alignedEnd = ((uintptr_t)baseAddr + size + sPageSize - 1) & ~(uintptr_t)(sPageSize - 1);
			if( mprotect((void*)alignedBegin, alignedEnd - alignedBegin, GetProt(permissionFlags)) == 0 )
				r = baseAddr;
			else
				r = nullptr;
		}
		else
			r = mmap(baseAddr, size, GetProt(permissionFlags), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		return r;
	}

	// decommits only the host pages which are entirely inside the range, since partially covered pages may still be in use by neighbouring allocations
	// callers which know that a partially covered page is no longer used should pass the page aligned range (see MMURange::unmapMem)
	void FreeMemory(void* baseAddr, size_t size, bool fromReservation)
	{
		if (fromReservation)
		{
			const uintptr_t alignedBegin = ((uintptr_t)baseAddr + sPageSize - 1) & ~(uintptr_t)(sPageSize - 1);
			const uintptr_t alignedEnd = ((uintptr_t)baseAddr + size) & ~(uintptr_t)(sPageSize - 1);
			if (alignedEnd > alignedBegin)
				mprotect((void*)alignedBegin, alignedEnd - alignedBegin, PROT_NONE);
		}
		else
			munmap(baseAddr, size);
	}

};
