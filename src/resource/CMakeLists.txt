add_library(CemuResource)

cemu_use_precompiled_header(CemuResource)

# .S files are used for resource embedding currently, we can drop this once C/C++ #embed is generally available
enable_language(ASM)

# icon resources
target_sources(CemuResource PRIVATE
embedded/resources.cpp
embedded/resources.h
)

target_sources(CemuResource PRIVATE embedded/fontawesome_macos.S)

target_sources(CemuResource PRIVATE CafeDefaultFont.cpp)

target_include_directories(CemuResource PUBLIC "../")

target_link_libraries(CemuResource PRIVATE CemuCommon)
