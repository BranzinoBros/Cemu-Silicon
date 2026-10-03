# Same as vcpkg's stock arm64-osx triplet, plus a deployment target that
# matches CMAKE_OSX_DEPLOYMENT_TARGET in the top-level CMakeLists.txt.
# Without it, ports build for the SDK's macOS version and the linker warns
# that they are newer than the app.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 26.0)
