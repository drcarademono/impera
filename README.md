# u5d - Ultima V Decompilation Project

## Introduction

This is an ongoing decompilation project of Ultima V: Warriors of Destiny.

Inspired by [u4-decompiled](https://github.com/ergonomy-joe/u4-decompiled), I started this project as a personal challenge to better understand the original game and its codebase.

## Goals

- Create a functionally equivalent version of the original game that can run on modern platforms.

- Match as much of the original code (v1.16 for MS-DOS) as possible.

Since the original linker (an unknown version of PLINK86) has not been found, perfect binary matching is currently impossible. Instead, the project focuses on assembly-level semantic matching.

When necessary, disassemblies from other platforms such as FM-TOWNS are also used as references.

## Current Status

### Matching Progress

- `ULTIMA.EXE`: approximately 95% matched, excluding assembly functions
- `*.OVL`: approximately 95% matched

### Playability

The game can be played through to the ending using the original data files. However, major and minor bugs may still remain throughout the game.

## Building

### Linux Target

Install a C compiler, CMake 3.16 or newer, pkg-config, SDL3, and SDL3_mixer
development packages. This target uses **SDL3_mixer 3.2 or newer**, not
SDL2_mixer. SDL3 must include your desktop's X11 or Wayland video driver.
If your distribution does not package these libraries, build them from the
official [SDL](https://github.com/libsdl-org/SDL) and
[SDL_mixer](https://github.com/libsdl-org/SDL_mixer) releases. The cloud build
was tested with SDL 3.4.18 and SDL_mixer 3.2.4.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
bash scripts/run-linux.sh
```

For libraries installed under a custom prefix, add its `lib/pkgconfig` (or
`lib64/pkgconfig`) directory to `PKG_CONFIG_PATH` before configuring.

The launcher copies the original data from `Ultima 5` to `build/runtime`
on first run and keeps saves in `build/runtime/SAVEGAME`. Existing runtime
files and saves are preserved on later runs. Set `U5D_DATA_DIR` to an absolute
path to use another data directory, or `U5D_BUILD_DIR` to an absolute path
if you configured a different build directory. Keep filename case intact.
New character creation requires the original `INIT.GAM` file (at least 4192
bytes), which supplies the starting party and world state. Add it to the data
directory if it is missing; `INIT.OOL` is a different file and cannot replace
it. Choose **Create New Character** before **Journey Onward** when there is
no active saved character.

Characters previously created without `INIT.GAM` have invalid starting state.
Back up `build/runtime/SAVEGAME`, restore the original `INIT.GAM`, and create
a new character. Rebuilding alone cannot repair an already damaged save.

Optional modern music and effects belong in `BGM` and `SFX` inside the data
directory; they are not included in the original DOS assets. Do not set
SDL's dummy video/audio drivers when playing on your desktop. Headless cloud
validation covered rendering, menu input, and the character-name prompt;
desktop graphics, audible output, and a complete playthrough are unverified.

Errors are written to the terminal and `build/runtime/LOG.TXT`. The log is
replaced on each launch, so copy it before restarting when reporting a bug.
For detailed tracing, run `U5D_DEBUG=1 bash scripts/run-linux.sh`; verbose logs
can grow quickly. If the log cannot be created, errors still go to the terminal.
Missing required map or NPC data now exits with an error naming the file
instead of retrying indefinitely.

To run the native settings and savegame regression checks:

```sh
cmake -S . -B build -DU5D_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

### Windows Target

Open `u5win/u5win.slnx` with Visual Studio 2026 and build the solution.

### MS-DOS Target (DJGPP)

Run `make` on a machine with DJGPP installed.

Depending on the environment, you may need to adjust the `CROSS_CC_PREFIX` value in the `Makefile`.

### MS-DOS Target (Original Toolchain)

Run `src\build.bat` on a machine or virtual machine with Microsoft C 5.1 installed.

The compiler installation path is `C:\MSC51`.

Note: executable linking is currently not possible. This target is used only for disassembly matching.
