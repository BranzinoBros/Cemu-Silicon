#pragma once

// minimal stand-in for Common/precompiled.h so the fiber implementations can be built without the rest of Cemu

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using uint32 = uint32_t;
using uint64 = uint64_t;

#define cemu_assert_debug(__cond) do { if (!(__cond)) { fprintf(stderr, "Assertion failed: %s\n", #__cond); abort(); } } while (0)
