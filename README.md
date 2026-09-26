# pcsx-abnxt - AutoBleem's Next PS1 Emulator

**pcsx-abnxt** is the next PS1 emulator for AutoBleem, based on upstream [notaz/pcsx_rearmed](https://github.com/notaz/pcsx_rearmed) at r26 with AutoBleem's features from `pcsx-ab` re-implemented on top. It runs on the **PlayStation Classic**, **Raspberry Pi**, **Windows** (development), and as the default emulator alongside `pcsx-ab` in AutoBleem packages.

## Features

pcsx-abnxt combines upstream PCSX-ReARMed's capabilities with AutoBleem's console-specific features:

* ARM/ARM64 dynamic recompiler by Ari64
* [lightrec](https://github.com/pcercuei/lightrec/) dynamic recompiler for x86 and other architectures
* NEON GPU (ARM NEON and x86 SSE2+), PCSX4ALL GPU
* Heavily modified P.E.Op.S. SPU
* BIOS HLE emulation (most games run without a BIOS file)
* **AutoBleem features**: The console's front buttons (Open, Reset, Power), resume points with save-state pictures (compatible with `pcsx-ab`'s layout), disc swapping with in-game menu, in-game filters, two-pad support, `SET_BY_PCSX` BIOS selection, per-serial game database

## Building

| Target | Command |
|--------|---------|
| **Windows** | `make` in an MSYS2 environment with SDL2 packages |
| **Raspberry Pi** | Cross-compile with the arm-linux-gnueabihf toolchain |
| **PlayStation Classic** | Cross-compile with the Sony ARM toolchain or the autobleem-build Docker image |
| **Docker (all targets)** | `docker/run.sh ci/build.sh <target>` using the autobleem-build image |

See `ci/build.sh` and the platform-specific Makefiles for details.

## Configuration

Save-state slots and memory cards are shared with `pcsx-ab` - switch between emulators on the same game without losing progress. Per-game settings are stored in `pcsx.custom.cfg` next to a game's save states.

## In-Game Controls

* **Home button** (or Select+Start on pads without Home) - open the menu
* **Open button** (console front panel) - open/close disc (for disc-swap games)
* **Reset button** (console front panel) - reset the game
* **Hold Home for 2 seconds** - exit and return to the launcher (on pads without direct power)

---

## Upstream PCSX-ReARMed

PCSX ReARMed is yet another PCSX fork based on the PCSX-Reloaded project, which itself contains code from PCSX, PCSX-df and PCSX-Revolution. This version was originally ARM architecture oriented (hence the name) with its MIPS->ARM dynamic recompilation and assembly optimizations, but more recently it targets other architectures too. The original upstream repo is at [notaz/pcsx_rearmed](https://github.com/notaz/pcsx_rearmed).

### Upstream Features

* ARM/ARM64 dynamic recompiler by Ari64
* [lightrec](https://github.com/pcercuei/lightrec/) dynamic recompiler for other architectures
* NEON GPU by Exophase for ARM NEON and x86 SSE2+
* PCSX4ALL GPU by Una-i/senquack for other architectures
* Heavily modified P.E.Op.S. SPU
* BIOS HLE emulation (most games run without proprietary BIOS)
* libretro support

### License

The emulator is licensed under the GNU GPLv2 (upstream). AutoBleem's additions and modifications are under GPL-3.0-or-later to align with the AutoBleem launcher's license.
