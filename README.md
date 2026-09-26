# pcsx-abnxt - AutoBleem's Next PS1 Emulator

**pcsx-abnxt** is the next PS1 emulator for AutoBleem, based on upstream [notaz/pcsx_rearmed](https://github.com/notaz/pcsx_rearmed) with AutoBleem's features re-implemented on top. It is shipped with AutoBleem and runs on the PlayStation Classic, Raspberry Pi, and as a development build for Windows.

## Building

```bash
make                                    # Build on Windows or native Linux
make -f Makefile.psc                    # Build for PlayStation Classic
```

See `ci/build.sh` for docker/remote build instructions and platform-specific toolchain setup.

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

See `COPYING` file (GNU General Public License version 2).
