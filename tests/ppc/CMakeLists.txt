# Wii U homebrew test programs for Cemu-Silicon's PowerPC emulation.
# Build with devkitPro's Wii U CMake wrapper (see README.md):
#   /opt/devkitpro/portlibs/wiiu/bin/powerpc-eabi-cmake -S tests/ppc -B build-ppc
#   cmake --build build-ppc
cmake_minimum_required(VERSION 3.13)
project(cemu_ppc_tests C)

if(NOT WIIU)
	message(FATAL_ERROR "Configure with devkitPro's Wii U toolchain: powerpc-eabi-cmake (or -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake)")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# -O2 keeps the benchmark loops tight; correctness tests don't depend on it
# because every tested instruction is emitted through inline asm
add_compile_options(-O2 -Wall -Wextra -Wno-unused-parameter)

add_executable(ppc_tests
	source/tests_main.c
	source/tests_int.c
	source/tests_mem.c
	source/tests_fpu.c
	source/tests_ps.c
)
wut_create_rpx(ppc_tests)

add_executable(ppc_bench
	source/bench_main.c
)
wut_create_rpx(ppc_bench)
