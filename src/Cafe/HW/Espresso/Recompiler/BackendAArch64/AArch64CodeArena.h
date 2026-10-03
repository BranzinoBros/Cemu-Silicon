#pragma once

// Executable memory for the AArch64 recompiler backend
// A single large MAP_JIT region is reserved on first use. Physical pages are only committed when code is written to them
// Finished code is bump-allocated into the region, so publishing a function costs a memcpy and an icache invalidate of the copied range
// Write access is toggled per thread via pthread_jit_write_protect_np (no syscalls, other threads keep executing from the arena meanwhile)
// Code is never freed
namespace AArch64CodeArena
{
	// copies size bytes of position-independent code into the arena and makes it executable
	// returns nullptr if the arena could not be reserved or is exhausted
	void* Commit(const void* code, size_t size);
}; // namespace AArch64CodeArena
