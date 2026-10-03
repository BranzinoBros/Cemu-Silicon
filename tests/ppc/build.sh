#!/usr/bin/env bash
# Builds ppc_tests.rpx and ppc_bench.rpx. Requires devkitPro with devkitPPC and
# wut installed (DEVKITPRO set), or run it inside the devkitpro/devkitppc
# docker image:
#   docker run --rm -v "$PWD":/src -w /src devkitpro/devkitppc tests/ppc/build.sh
# Output: tests/ppc/build/ppc_tests.rpx, tests/ppc/build/ppc_bench.rpx
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"

"$DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-cmake" -S "$HERE" -B "$HERE/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$HERE/build" --parallel

ls -l "$HERE/build/"*.rpx
