# Cemu-Silicon

A Wii U emulator for Apple Silicon Macs, forked from [Cemu](https://github.com/cemu-project/Cemu).

Cemu-Silicon runs on one platform only: Macs with Apple Silicon (M1 and later) on macOS 26 or newer. We dropped Windows, Linux and Intel Macs so every change can target that one machine:

- the native arm64 PowerPC recompiler (JIT) instead of Rosetta,
- the Metal renderer and Apple's unified memory instead of a Vulkan translation layer,
- performance and efficiency cores, 16 KB pages, and the ARMv8 crypto and NEON instructions every Apple chip has.

The goal is the fastest and most power-efficient Wii U emulation possible on a Mac, without giving up accuracy.

## Status

Early work in progress. There are no releases yet. If you want a stable emulator today, use [official Cemu](https://github.com/cemu-project/Cemu/releases).

## Relationship to Cemu

Cemu-Silicon is an independent fork. It is not affiliated with or endorsed by the Cemu team, so please don't report Cemu-Silicon problems to them.

Most of the code in this fork is written with AI assistance (Claude Code). Cemu's [contribution guidelines](https://github.com/cemu-project/Cemu/blob/main/CONTRIBUTING.md) don't accept AI-written code, so nothing from this fork is sent upstream. Fixes flow one way, from Cemu into this fork.

All credit for the emulator itself goes to Exzap and the Cemu contributors.

## Requirements

- A Mac with Apple Silicon (M1 or later)
- macOS 26 or later
- Your own Wii U game dumps and keys. Cemu-Silicon does not include or download any games.

## Building

See [BUILD.md](/BUILD.md).

## License

Cemu-Silicon is licensed under the [Mozilla Public License 2.0](/LICENSE.txt), like Cemu. The exceptions are the files in the dependencies directory, which keep their original licenses, and the individual files in `src` whose headers say otherwise.
