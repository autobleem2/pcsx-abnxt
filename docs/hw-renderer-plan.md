# Rendering above 2x: a GPU (shader) renderer - plan

Status: a plan only (2026-09-28). The emulator code is frozen; this work happens only in the owner's own sessions.

## The question

The owner asked two things:
- Can the built-in gpu plugin render at 3x, 4x, 5x... up to a theoretical 4K frame?
- Can it do that on the GPU, with shaders, rather than in software?

The PC, the Pis and Windows need it. The PSC will be tried by hand, even if it chokes.

## What we have today

- **gpu_neon, the built-in plugin** (`plugins/gpu_neon/psx_gpu`), rasterises on the CPU.
  - Its "enhancement" is a fixed 2x: 4 x 1024x1024 buffers, a 2048 pitch, and `*_4x` sprite paths.
  - The NEON assembly is written for exactly that layout.
  - Making it take N means rewriting the rasteriser. The cost grows with N squared on the CPU (4x is 4 times the work of 2x), so this is a dead end for 3x and up.
- **gpu_unai and gpu_peops (dfxvideo)** are CPU renderers at 1x (unai has its own tricks, no real upscale).
- **gpu-gles** (`plugins/gpu-gles`) is the old P.E.Op.S. OpenGL plugin, Pandora era.
  - It is GLES 1.x fixed-function (`glOrtho`, no shaders) with its own EGL/X11 window.
  - It draws the polygons at window resolution, so its internal resolution is whatever the window is.
  - It is weak on framebuffer effects (offscreen drawing, VRAM reads and writes), so many games glitch.
  - It is not maintained.
- **Our output** (`plat_autobleem`) is a GLES 2.0 context, desktop GL 2.1 as the fallback. It takes an RGB565 frame and runs the filter, scanlines and HUD passes.

## Answer: yes, but as a new GPU renderer, not by stretching gpu_neon

Real upscaling means the triangles themselves are rasterised at N times the resolution. Any shader applied to a finished 1x frame only interpolates; that is what our filters already do. So the renderer has to draw the PlayStation's primitives with GL:
- VRAM becomes a texture N times its size;
- texture pages and CLUTs are looked up in a fragment shader;
- semi-transparency modes become blend states;
- the mask bit, dithering and 24-bit display run in shaders;
- CPU reads of VRAM go through a readback at 1x.

That is what Beetle PSX HW, DuckStation/SwanStation and ePSXe's plugins do.

**The limits per platform:**
- VRAM at N times its size is (1024N)x(512N), so N <= `GL_MAX_TEXTURE_SIZE` / 1024.
  - 4096 allows 4x; 8192 allows 8x; 16384 allows 16x.
  - A 4K picture of a 240-line game needs 9x, so 16384-class GPUs only (desktop).
  - The Pi 4/400 (V3D 4.2, 4096 or 8192) tops out at about 4x.
  - PSC: the PowerVR GE8300 is to be checked. It is probably 4096, so 4x at most, and 2-3x is realistic.
- Memory at 4x: 4096x2048 RGBA8 = 32 MB, plus a scratch copy. Fine everywhere.

## Route

**Phase 0 - facts (a day, cheap):**
- `plat_autobleem` logs `GL_VERSION`, `GL_SHADING_LANGUAGE_VERSION`, `GL_MAX_TEXTURE_SIZE` and the extensions at start, and tries a GLES 3.0 context first.
- Run it on the PSC, the Pi 400, the PC stick and Windows.
- This decides GLES 3.0 (integer textures, `texelFetch`, the easy path) or GLES 2.0 (16-bit VRAM packed in RGBA8/RG8, harder shaders).

**Phase 1 - pick the donor (a spike, 2-3 days):**
- **a) Beetle PSX HW's GL renderer** (`rsx/rsx_lib_gl`, C++; GL 3.3 core, so it has to be ported to GLES 3.0).
  - It is proven at 1x-16x, with the mask bit, dithering and 24-bit display handled.
  - It takes per-primitive calls (`rsx_intf_push_triangle/quad/line`, `load_image`, `fill_rect`, `copy_rect`, `read_vram`), so we write one gpulib command parser that calls them. gpu_neon's `psx_gpu_parse.c` is the model.
  - Licence: to be checked, but GPL-2-compatible is expected.
- **b) SwanStation's GPU_HW** is GLES 3 already and very accurate. But it is GPL-3 and tied to DuckStation's core classes, so extracting it is a large job.
- **c) gpu-gles moved to GLES 2** means rewriting all its fixed-function drawing, and its accuracy stays poor. Worth it only as a quick look.

Recommendation: a), with b) as the reference for GLES quirks.

**Phase 2 - `gpu_hw`, a gpulib renderer in our GL context (the bulk, weeks):**
- It is a new plugin/renderer that is built in next to gpu_neon and chosen in the menu ("Renderer: Software / GPU"); gpu_neon stays the default and the fallback.
- It draws into its own FBO in the context that `plat_autobleem` owns (one context, no second window).
- It needs a new `plat_ab_present_tex(tex, w, h, dst)`: the filter pass reads the GL texture directly, skipping the RGB565 upload. Scanlines and the HUD are unchanged.
- An internal scale setting: 1x..Nmax, capped from `GL_MAX_TEXTURE_SIZE` and a platform ini entry.
  - Offered per platform: the PSC would show 1x-4x with a "may be slow" note, since the owner wants to try.
  - Only whole steps, because the output shaders do the rest.
- VRAM sync:
  - CPU reads of VRAM (movies, save pictures, effects) come from a 1x readback, done only when asked (dirty rectangles);
  - `load_image` uploads to the scaled texture.
- The resume picture, the menu's frame and the screenshots are one `glReadPixels` of the finished frame, scaled down to what those consumers take today.
- Save states: VRAM is read back at 1x before a save and uploaded after a load. The states stay compatible with gpu_neon.

**Phase 3 - accuracy and speed:**
- a game list (2D, 3D, FMV, known framebuffer-effect titles);
- compared with gpu_neon at 1x, and on the PSC with fps before and after (the emulator-changes rule);
- then optional 2x/4x MSAA, and "texture filtering: nearest / xBR" in the renderer.

**Phase 4 - the launcher:**
- the renderer and internal-scale rows in the game options menu;
- `abfeatures` gets a `hwrender` token, so the launcher offers them only to an emulator that has them.

## Risks

- Framebuffer-effect games (VRAM reads, draws into the displayed area): this is the known hard part of every HW renderer, and why a donor is better than writing our own.
- PSC driver quality (PowerVR under Sony's Weston): the GLES 3 context may not be offered; then it is GLES 2 or nothing.
- The CPU cost of the command parser and draw calls on the PSC's A35 at 60 fps, before the fill rate even matters.
- It is effort only Opus should do (emulator code), in owner sessions.
