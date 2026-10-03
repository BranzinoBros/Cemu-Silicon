# Standalone self-test for the fiber implementations
# Can be built as part of Cemu (-DENABLE_FIBER_SELFTEST=ON) or on its own:
#   cmake -S src/util/Fiber/tests -B build-fibertest -DCMAKE_BUILD_TYPE=Release && cmake --build build-fibertest
cmake_minimum_required(VERSION 3.21)
project(CemuFiberSelfTest CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

function(add_fiber_selftest targetName fiberSource)
	add_executable(${targetName} FiberSelfTest.cpp ../${fiberSource})
	target_include_directories(${targetName} PRIVATE ..)
	target_compile_options(${targetName} PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/FiberSelfTestPrelude.h)
	target_link_libraries(${targetName} PRIVATE Threads::Threads)
endfunction()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
	add_fiber_selftest(FiberSelfTest FiberAArch64.cpp)
	target_compile_definitions(FiberSelfTest PRIVATE FIBER_SELFTEST_HAS_GUARD_PAGE=1)
endif()

# ucontext based implementation, useful as a baseline for the switch rate
add_fiber_selftest(FiberSelfTestUcontext FiberUnix.cpp)
if(APPLE)
	target_compile_definitions(FiberSelfTestUcontext PRIVATE _XOPEN_SOURCE)
	target_compile_options(FiberSelfTestUcontext PRIVATE -Wno-deprecated-declarations)
endif()
