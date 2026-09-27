# The GLES render pipeline (plan, 2026-09-28)

The frame used to go through SDL_Renderer: CPU smoothing (scale2x/eagle2x/hq2x/hq3x in `ab/ab_scaler.c`)
into a bigger `shadow_fb`, one streaming texture, a "Sharp" prescale render target, a scanline overlay the
size of the picture, and the HUD as SDL textures created and destroyed as its text changed. It is replaced by
our own GLES path in libpicofe's `plat_autobleem.c` (was `plat_sdl2.c`); SDL keeps the window, the GL context and the input.

## The pipeline, per frame

1. The GPU plugin draws the PS1 frame; plugin_lib copies it at 1x into `shadow_fb` (RGB565). That buffer is
   also the picture the resume point and the screenshots take - the clean console frame, never scanlines,
   filters or the HUD.
2. One 1x upload to a texture.
3. Smoothing, when on: one shader pass into an FBO at k x the frame (scale2x, eagle2x = 2x).
4. The scaling filter: one shader pass from that texture onto the screen, into `dst` - the rect the scaler
   mode (1x1, integer 2x, scaled 4:3, integer 4:3, fullscreen) puts the picture in.
5. The scanlines over the whole screen: a 1 x H texture (H = the screen's height), stretched across, made
   only when the screen's height, the level or the alpha changes. Counted in screen pixels - level 1 = 1 dark
   + 1 bright row, 2 = 1 dark + 2 bright, 3 = 2 dark + 1 bright; the alpha is its own setting. Off while a
   CRT filter is on (the CRT draws its own).
6. The HUD over the whole screen, at screen resolution: images uploaded once into fixed slots (updated in
   place when their contents change - no texture made or freed during a game), drawn as quads. The aspect
   ratio changes nothing here; they follow only the screen's size.
7. A frame a debug driver asked for: `glReadPixels` before the swap.
8. `SDL_GL_SwapWindow`.

The menu is a window-sized RGB565 buffer presented 1:1 without scanlines or HUD.

## Shaders

In libretro's single-file GLSL shape (`#if defined(VERTEX)` / `#elif defined(FRAGMENT)`, `VertexCoord`,
`TexCoord`, `MVPMatrix`, `Texture`, `TextureSize`, `InputSize`, `OutputSize`, `FrameCount`), compiled with a
`#version 100` prefix on GLES and `#version 120` on desktop GL. The sources are the frontend's
(`frontend/ab/ab_shaders.c`, GPLv2+ like the libretro CRT shaders it carries) and are handed to libpicofe by
pointer; libpicofe (GPL2+/LGPL2.1+/MAME) carries only its own pass-through.

| stage | PSC | other platforms (later) |
|---|---|---|
| smoothing | scale2x, eagle2x | + Super Eagle, hq2x/3x, xBRZ (licences checked per file) |
| filter | nearest, linear, sharp-bilinear (replaces "Sharp"), sharp-bilinear-simple, quilez, zfast_crt, crt-pi | + better CRTs |

On the PSC a CRT filter and smoothing are exclusive (a CRT turns smoothing off). Measured on the console
(2026-09-27): scale2x + quilez 10.7 ms, + zfast_crt 15.3 ms, + crt-pi 17.5 ms a frame.

## Milestones

- **M1 (this branch)**: libpicofe `plat_autobleem` (`plat_ab_*`, renamed from `plat_sdl2`) on a GL window + context (GLES 2.0 asked for, a desktop GL
  2.1 context as the fallback), the passes above, the scanline texture, the HUD slot API, `glReadPixels`
  shots; the frontend without CPU smoothing where the GPU does it (`pl_gpu_smooth`), the filter list, the
  HUD through the slot API, the battery icon only when a pad is low (the show-while-Home-is-held is gone:
  Home opens the menu and, held, leaves the game).
- **M2**: the in-game menu - sections (Game / Picture / Controllers / Settings / Exit), a header with the
  game's name and every pad's battery, the last auto screenshot as a thumbnail from RAM, a TTF font and
  `lang/*.txt` for every language, sized to fit the screen. The HUD's text (messages, FPS, CPU) in the
  same TTF font, rendered to its slot only when the text changes; the FPS line labelled - the frames the
  game drew in the last second and the emulated refreshes per second (the emulation's speed, 60 NTSC /
  50 PAL), e.g. `FPS 59 · 60.0 Hz` - and the CPU load as `CPU 45%`.
- **M3**: the launcher's contract - `ab_*` keys in `pcsx.cfg` (`ab_scaler`, `ab_smoothing`, `ab_filter`,
  `ab_scanlines`, `ab_scanline_alpha`) behind an `abfeatures` token, `-ab-*` options for the defaults (the old
  `-filter`/`-ratio` stay as aliases), an `abrender` file next to the binary listing what the build has; the
  launcher's scaler modes, scanline levels and per-platform filter lists.
- **M4**: precompiled shader binaries (made on the console for the PSC, a cache on first run elsewhere),
  ANGLE on Windows (`libEGL.dll`, `libGLESv2.dll`), the other platforms' extra shaders.
