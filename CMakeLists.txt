cmake_minimum_required(VERSION 3.21.1)

option(ENABLE_VCPKG "Enable the vcpkg package manager" ON)
option(MACOS_BUNDLE "The executable when built on macOS will be created as an application bundle" OFF)
option(ALLOW_PORTABLE "Allow Cemu to be run in portable mode" ON)

# used by CI script to set version:
set(EMULATOR_VERSION_MAJOR "0" CACHE STRING "")
set(EMULATOR_VERSION_MINOR "0" CACHE STRING "")
set(EMULATOR_VERSION_PATCH "0" CACHE STRING "")

set(THREADS_PREFER_PTHREAD_FLAG true) # needs to be set before vcpkg

execute_process(
		COMMAND git log --format=%h -1
		WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
		OUTPUT_VARIABLE GIT_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_definitions(-DEMULATOR_HASH=${GIT_HASH})

if (ENABLE_VCPKG)
	# check if vcpkg is shallow and unshallow it if necessary
	execute_process(
	COMMAND git rev-parse --is-shallow-repository
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/dependencies/vcpkg
	OUTPUT_VARIABLE is_vcpkg_shallow
	OUTPUT_STRIP_TRAILING_WHITESPACE
    )

	if(is_vcpkg_shallow STREQUAL "true")
		message(STATUS "vcpkg is shallow. Unshallowing it now...")
		execute_process(
			COMMAND git fetch --unshallow
			WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/dependencies/vcpkg"
			RESULT_VARIABLE result
			OUTPUT_VARIABLE output
		)
	endif()

	set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports_mac;${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports")
	set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_triplets")
	set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/vcpkg/scripts/buildsystems/vcpkg.cmake"
		CACHE STRING "Vcpkg toolchain file")
	# Set this so that all the various find_package() calls don't need an explicit
	# CONFIG option
	set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
endif()

# Apple Silicon only. macOS 26 is the first release with Metal 4.
# Keep in sync with dependencies/vcpkg_overlay_triplets/arm64-osx.cmake
# and MACOSX_MINIMUM_SYSTEM_VERSION in src/CMakeLists.txt.
set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")

project(Cemu VERSION 2.0.0)

if (NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
	message(FATAL_ERROR "Cemu-Silicon only supports macOS on Apple Silicon (arm64)")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions($<$<CONFIG:Debug>:CEMU_DEBUG_ASSERT>) # if build type is debug, set CEMU_DEBUG_ASSERT

add_definitions(-DEMULATOR_VERSION_MAJOR=${EMULATOR_VERSION_MAJOR})
add_definitions(-DEMULATOR_VERSION_MINOR=${EMULATOR_VERSION_MINOR})
add_definitions(-DEMULATOR_VERSION_PATCH=${EMULATOR_VERSION_PATCH})

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# enable link time optimization for release builds
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)

if(CMAKE_GENERATOR STREQUAL "Xcode")
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY XCODE_STARTUP_PROJECT CemuBin)
endif()

enable_language(OBJC OBJCXX)

option(ENABLE_VULKAN "Enables the Vulkan backend" ON)
option(ENABLE_METAL "Enables the Metal backend" ON)
option(ENABLE_DISCORD_RPC "Enables the Discord Rich Presence feature" ON)

# input backends
option(ENABLE_HIDAPI "Build with HIDAPI" ON)
option(ENABLE_SDL "Enables the SDLController backend" ON)
option(ENABLE_LIBUSB "Build with libusb support" ON)

if (ENABLE_LIBUSB)
	add_compile_definitions(HAS_LIBUSB)
endif()

# audio backends
option(ENABLE_CUBEB "Enabled cubeb backend" ON)

option(ENABLE_WXWIDGETS "Build with wxWidgets UI (Currently required)" ON)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(pugixml REQUIRED)
find_package(RapidJSON REQUIRED)
find_package(Boost COMPONENTS program_options filesystem nowide REQUIRED)
find_package(libzip REQUIRED)
find_package(glslang REQUIRED)
find_package(ZLIB REQUIRED)
find_package(zstd MODULE REQUIRED) # MODULE so that zstd::zstd is available
find_package(OpenSSL COMPONENTS Crypto SSL REQUIRED)
find_package(glm REQUIRED)
find_package(fmt 12.1 REQUIRED)
find_package(PNG REQUIRED)

if(ENABLE_SDL)
	find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
	add_compile_definitions(HAS_SDL)
endif()

# glslang versions older than 11.11.0 define targets without a namespace
if (NOT TARGET glslang::SPIRV AND TARGET SPIRV)
	add_library(glslang::SPIRV ALIAS SPIRV)
endif()

if (ENABLE_VULKAN)
	include_directories("dependencies/Vulkan-Headers/include")
	add_compile_definitions(ENABLE_VULKAN)
	add_compile_definitions(VK_NO_PROTOTYPES)
endif()

if (ENABLE_METAL)
    include_directories(${CMAKE_SOURCE_DIR}/dependencies/metal-cpp)

    add_compile_definitions(ENABLE_METAL)
endif()

if (ENABLE_DISCORD_RPC)
	add_compile_definitions(ENABLE_DISCORD_RPC)
endif()

if (ENABLE_HIDAPI)
	set(SUPPORTS_WIIMOTE ON)
	add_compile_definitions(HAS_HIDAPI)
endif ()

if (ENABLE_WXWIDGETS)
	find_package(wxWidgets 3.3 REQUIRED COMPONENTS base core propgrid xrc)
endif()

if (ENABLE_CUBEB)
	if (NOT ENABLE_VCPKG)
	    find_package(cubeb)
	endif()
	if (NOT cubeb_FOUND)
		option(BUILD_TESTS "" OFF)
		option(BUILD_TOOLS "" OFF)
		option(BUNDLE_SPEEX "" OFF)
		add_subdirectory("dependencies/cubeb" EXCLUDE_FROM_ALL SYSTEM)
		add_library(cubeb::cubeb ALIAS cubeb)
	endif()
	add_compile_definitions("HAS_CUBEB=1")
endif()

add_subdirectory("dependencies/ih264d" EXCLUDE_FROM_ALL)

add_subdirectory("dependencies/xbyak_aarch64" EXCLUDE_FROM_ALL)

find_package(ZArchive QUIET)
if (NOT ZArchive_FOUND)
	add_subdirectory("dependencies/ZArchive" EXCLUDE_FROM_ALL)
endif()

add_subdirectory(src)
