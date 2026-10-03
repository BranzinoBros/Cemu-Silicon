# Build Instructions

## Table of Contents

- [macOS](#macos)
   - [Installing brew](#installing-brew)
   - [Dependencies](#dependencies)
   - [MoltenVK](#moltenvk)
   - [Build Cemu using CMake](#build-cemu-using-cmake)
- [Updating Cemu and source code](#updating-cemu-and-source-code)
- [CMake configure flags](#cmake-configure-flags)

## macOS

Cemu-Silicon runs on Apple Silicon Macs with macOS 26 or later, and builds with Xcode 26 or later
(the macOS 26 SDK is required).

### Installing brew

To install the dependencies required to build Cemu, you will need to install Homebrew package manager first. You can do this by running the following command in your terminal:

1. `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`
2. Set up the Homebrew shell environment: eval `"$(/opt/homebrew/bin/brew shellenv)"`

### Dependencies

The following dependencies are required. You can install them using Homebrew with the following command:

`brew install automake boost cmake git libtool nasm ninja pkgconf`

### MoltenVK

Cemu uses the MoltenVK library to provide Vulkan support on macOS. While available on Brew, Cemu requires the "privateapi" version of MoltenVK, which is not available on Brew. To install the required version, follow the instructions below:

1. `curl -L -O https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.1/MoltenVK-macos-privateapi.tar`
1. `tar xf MoltenVK-macos-privateapi.tar`
1. Create a directory for the MoltenVK dylib if it doesn't already exist: `sudo mkdir -p /opt/homebrew/lib`
1. Copy the MoltenVK dylib to your system library directory: `sudo cp MoltenVK/lib/libMoltenVK.dylib /opt/homebrew/lib/`

Alternatively, you can use the non-privateapi version of MoltenVK, but you may encounter some rendering issues due to the lack of logicOp support. If you want to go this route, simply install MoltenVK from Brew with `brew install molten-vk` and skip the steps above.

### Build Cemu using CMake

1. `git clone --recursive https://github.com/cemu-project/Cemu`
2. `cd Cemu`
3. `cmake -S . -B build -DCMAKE_BUILD_TYPE=release -G Ninja`
4. `cmake --build build`
5. You should now have a Cemu executable file in the /bin folder, which you can run using `./bin/Cemu_release`.

#### Creating an app bundle
- If you want to create an app bundle instead of a raw executable, append the following flag to the command in step 3:
   - `-DMACOS_BUNDLE=ON`

#### Troubleshooting steps
- If step 3 gives you an error about not being able to find ninja, try appending the following to the command and try again: `-DCMAKE_MAKE_PROGRAM=/opt/homebrew/bin/ninja`

## Updating Cemu and source code
1. To update your Cemu local repository, use the command `git pull --recurse-submodules` (run this command in the Cemu root).
    - This should update your local copy of Cemu and all of its dependencies.
2. Then, you can rebuild Cemu using the steps listed above.

If CMake complains about Cemu already being compiled or another similar error, try deleting the `CMakeCache.txt` file inside the `build` folder and retry building.

## CMake configure flags
Some flags can be passed during CMake configure to customise which features are enabled on build.

Example usage: `cmake -S . -B build -DCMAKE_BUILD_TYPE=release -DENABLE_SDL=ON -DENABLE_VULKAN=OFF`

### General
| Flag               |   | Description                                                                 | Default | Note               |
|--------------------|:--|-----------------------------------------------------------------------------|---------|--------------------|
| ALLOW_PORTABLE     |   | Allow Cemu to use the `portable` directory to store configs and data        | ON      |                    |
| CEMU_CXX_FLAGS     |   | Flags passed straight to the compiler, e.g. `-march=native`, `-Wall`        | ""      |                    |
| ENABLE_CUBEB       |   | Enable cubeb audio backend                                                  | ON      |                    |
| ENABLE_DISCORD_RPC |   | Enable Discord Rich presence support                                        | ON      |                    |
| ENABLE_HIDAPI      |   | Enable HIDAPI (used for Wiimote controller API)                             | ON      |                    |
| ENABLE_SDL         |   | Enable SDLController controller API                                         | ON      |                    |
| ENABLE_VCPKG       |   | Use VCPKG package manager to obtain dependencies                            | ON      |                    |
| ENABLE_VULKAN      |   | Enable the Vulkan graphics backend                                          | ON      |                    |
| ENABLE_WXWIDGETS   |   | Enable wxWidgets UI                                                         | ON      | Currently required |
| ENABLE_LIBUSB      |   | Enable libusb                                                               | ON      |                    |

### macOS
| Flag         | Description                                    | Default |
|--------------|------------------------------------------------|---------|
| MACOS_BUNDLE | macOS executable will be an application bundle | OFF     |
