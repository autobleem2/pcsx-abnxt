# pcsx-abnxt - AutoBleem's Next PS1 Emulator

**pcsx-abnxt** is the next PS1 emulator for AutoBleem, based on upstream [notaz/pcsx_rearmed](https://github.com/notaz/pcsx_rearmed) with AutoBleem's features re-implemented on top. It is shipped with AutoBleem and runs on the PlayStation Classic, Raspberry Pi, and as a development build for Windows.

## Building

```bash
ci/build.sh psc|rpi|rpi64|pcusb|all   # in AutoBleem's autobleem-build Docker image
./make_psc.sh                          # the console, on the build server over ssh
./make_rpi.sh / ./make_rpi64.sh        # Raspberry Pi 32-bit / 64-bit
./make_win.sh                          # a Windows development build (MSYS2)
```

Each leaves `pcsx-ab` + `plugins/*.so` in `build_<target>/dist/`, laid out as AutoBleem's `Autobleem/bin/emu/` wants them. The header of each script has the details.

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
