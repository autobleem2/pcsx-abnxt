# The render pipeline's shaders

libretro's single-file GLSL shape (see libpicofe's `plat_autobleem.h`), embedded as C strings by
`embed.py` into `../ab_shaders_src.h`; `../ab_shaders.c` says which pass each one is.

| file | pass | origin | licence |
|---|---|---|---|
| `scale2x.glsl`, `eagle2x.glsl` | smoothing, 2x | ours (the rules of `ab_scaler.c`) | GPLv2+, as the rest of the frontend |
| `sharp-bilinear.glsl` | filter | libretro glsl-shaders, Themaister | public domain |
| `sharp-bilinear-simple.glsl` | filter | libretro glsl-shaders, rsn8887 | public domain |
| `quilez.glsl` | filter | libretro glsl-shaders, after Inigo Quilez's "Improved texture interpolation" | no licence text in the file (to be checked before a release) |
| `zfast_crt.glsl` | filter (CRT) | libretro glsl-shaders, Greg Hogan | GPLv2+ |
| `crt-pi.glsl` | filter (CRT) | libretro glsl-shaders, davej | GPLv2+ |

Changes to the libretro files: `quilez.glsl`'s credit line in ASCII, `zfast_crt.glsl` with LF line ends.
The two CRT shaders soften their scanlines where a line's height on the screen is not a whole number of
pixels (`soft` / `lineAA.x`: 0 at 720p's 3 px or 4K's 9 px, 1 at 1080p's 4.5 px): `crt-pi.glsl` lighter
gaps and a wider beam, `zfast_crt.glsl` its scanline averaged over three points of the pixel (crt-pi's
MULTISAMPLE) and a little softer - lines of 4 and 5 pixels showed as a pattern. A whole scale draws as the
originals do. The platform tells both the PlayStation's line count (libpicofe `plat_ab_set_filter_picture`),
not the 2x or smoothed frame's.
