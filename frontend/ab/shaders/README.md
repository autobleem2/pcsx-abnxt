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
| `crt-raster.glsl` | filter (CRT) | ours | GPLv2+, as the rest of the frontend |

`crt-raster.glsl` ("CRT (raster)", the owner's idea of 2026-10-12): a tube has no pixels, so the two parts
of its look are drawn at different resolutions - the beam per line of the PlayStation's picture (a Gaussian
of the line's colour, wider when brighter, its area kept so the picture's brightness is too, widened by a
screen pixel's height so lines of 4 and 5 pixels stay even), and the phosphor mask per pixel of the screen
(`gl_FragCoord`: aperture grille, slot, delta or none - the menu's "CRT mask", built as one program per
mask through `CRT_MASK`, `ab_shaders.c`). Linear light in between. The mask is on the picture only: the
screen halo's bands come from its 16 x 16 average, where a 2-3 pixel mask is gone.

Changes to the libretro files: `quilez.glsl`'s credit line in ASCII, `zfast_crt.glsl` with LF line ends.
The two CRT shaders soften their scanlines where a line's height on the screen is not a whole number of
pixels (`soft` / `lineAA.x`: 0 at 720p's 3 px or 4K's 9 px, 1 at 1080p's 4.5 px): `crt-pi.glsl` lighter
gaps and a wider beam, `zfast_crt.glsl` its scanline averaged over three points of the pixel (crt-pi's
MULTISAMPLE) and a little softer - lines of 4 and 5 pixels showed as a pattern. A whole scale draws as the
originals do. The platform tells both the PlayStation's line count (libpicofe `plat_ab_set_filter_picture`),
not the 2x or smoothed frame's.
