# gpu_neon at 3x, 4x ... N: plan

Status: an analysis and a plan (2026-09-28), nothing built. The emulator code is frozen, so this work happens only in the owner's own sessions. The owner chose this route over a new plugin (`hw-renderer-plan.md` is kept only as the rejected alternative).

## How the 2x "enhancement" works today

Files: `plugins/gpu_neon/psx_gpu_if.c`, `psx_gpu/psx_gpu_parse.c` (`gpu_parse_enhanced`), `psx_gpu/psx_gpu_4x.c`, `plugins/gpulib/vout_pl.c`.

- **Every primitive is drawn twice.**
  - The first pass goes into the normal 1x VRAM (`vram_ptr`). It keeps VRAM correct for texture reads, CPU reads and save states.
  - The second pass goes into an *enhancement buffer* (`vram_out_ptr` switched by `enhancement_enable()`).
  - Textures are always read from the 1x VRAM, so only the geometry gets finer, the same as with GPU renderers.
- **The second pass scales the geometry:**
  - `shift_vertices3()` does `x <<= 1, y <<= 1` on the prepared triangle;
  - `triangle_area *= 4`;
  - the viewport is `*2` (`+1` on the ends) and clamped to 1024 wide.
  - Sprites take their own path, `render_sprite_4x()` (every texel as 2x2 pixels, `psx_gpu_4x.c`). Fills use `render_block_fill_enh()` with doubled coordinates.
- **The buffers:**
  - 4 of them, each 1024x1024 u16 (`ENHANCEMENT_BUF_SIZE`, `select_enhancement_buf_by_index` = `i << 20`).
  - Each follows one *scanout* (a display area in VRAM: `update_enhancement_buf_scanouts()`, a double-buffering heuristic).
  - A VRAM upload, copy or fill outside the pass is copied up by `sync_enhancement_buffers()` with `scale2x_tiles8()`.
- **The output:**
  - `vout_pl.c` doubles `w_out`/`h_out`;
  - `get_enhancement_bufer()` hands over the buffer with `x,y,w,h *= 2`;
  - `pl_vout_flip()` reads it with the fixed 1024-pixel pitch.
- **It is on only when** `hres <= 512 && vres <= 256`, not in 24-bit mode, and the scanout is not at the bottom of VRAM. Hi-res (640x480) games are never enhanced.

## What ties it to 2x

1. **The 1024-pixel pitch is compiled in everywhere.**
   - The rasteriser writes with `fb_ptr + y*1024 + x`: `lsl #11` and `#2048` in `psx_gpu_arm_neon.S` (about 60 places), plus about 130 in `psx_gpu.c` / `psx_gpu_simd.c` / `psx_gpu_4x.c`.
   - A 2x frame of a game up to 512 wide fits in it exactly.
2. **The span buffers:**
   - `MAX_SPANS 512`: a triangle, after clipping to the viewport, may be at most 512 lines tall. 2x of 256 is 512.
   - `MAX_BLOCKS_PER_ROW 128`: 128 blocks of 8 pixels, so 1024 per row.
3. **The factor 2 is hard-coded in C:**
   - the vertex shift and the area;
   - the viewport;
   - `get_enhancement_bufer`;
   - `vout_pl.c` (`*= 2` in 3 places);
   - the sprite path (2x2 per texel, 2x only);
   - the uploads' `scale2x_tiles8` (NEON for 2x);
   - the `uv_hack` texture adjustments, tuned on 2x.

## The plan: keep the rasteriser, generalise everything around it

The NEON/SIMD inner loops (block fill, shading, texturing) stay as they are: 1024 pitch, 1x and 2x speed unchanged. What changes is what feeds them and where their output goes.

### Step 1 - the factor N and taller spans (up to what fits in 1024 columns)

- `N` replaces 2:
  - vertices `*= N` (not `<<= 1`), `triangle_area *= N*N`;
  - viewport `*N` and `+ N-1`;
  - `get_enhancement_bufer`, `vout_pl.c`;
  - the buffer's height is `N*256` (then `N*vres`).
- `MAX_SPANS` goes to `16*512`, so a triangle can be as tall as the frame. The struct layout changes, so `psx_gpu_offsets.h` is regenerated with `psx_gpu_offsets_update.c`; the assembly reads those offsets.
- `scale2x_tiles8` becomes `scaleN_tiles` in C for N != 2 (the NEON 2x stays).
- Sprites at N: a C `render_sprite_nx` (every texel as NxN) for N != 2; `render_sprite_4x` stays for 2x. The alternative is the `#if 0` two-triangle path already in `psx_gpu_parse.c`, but it risks seams in 2D games.
- The enhanced pass draws *without dithering* for N >= 3. The 4x4 pattern at N times the resolution only makes noise; this is the same as the others' "true colour at high resolution".
- Allowed while `N*hres <= 1024`:
  - 320-wide games: 3x (960);
  - 256-wide games: 4x (1024);
  - 368-512 wide: 2x as now.

### Step 2 - vertical strips: any N, and the 640-wide games

- The N-times frame is split into strips 1024 wide: `ceil(N*hres / 1024)` of them.
- Each strip is a buffer of its own: 1024 x `N*vres`, the same pitch the rasteriser knows.
- The enhanced pass draws every primitive once per strip it touches. The vertices are moved by `-strip*1024` and the viewport is clipped to the strip. Pixels are drawn once overall; only the triangle setup repeats for the few primitives that cross a strip's edge.
- The limit `hres <= 512 && vres <= 256` goes: 640x480 games get N too.
- Memory per scanout is `strips * 1024 * N*vres * 2` bytes; there are 4 scanouts.
  - 4x of 320x240 (1280x960): 2 strips, about 4 MB per scanout, 16 MB in total.
  - 9x of 320x240 (2880x2160, "4K"): 3 strips, about 12 MB per scanout.
  - 8x of 640x480: 5 strips, about 40 MB per scanout.

### Step 3 - the GPU's part: assembling and scaling (in `plat_autobleem`)

- There is a new flip, `pl_vout_flip_strips(strips[], n, strip_h, w, h)`, alongside `pl_vout_flip()`.
- Each strip is uploaded with `glTexSubImage2D` as a full 1024-wide block into one texture that is `n*1024` wide. That needs no row length, so it is GLES 2 friendly and there is no CPU copy into a shadow buffer.
- The filter pass crops with its texture coordinates and scales to the screen, as today. Scanlines and the HUD are unchanged.
- The CPU smoothing (hq2x/3x) and the GPU scale2x/eagle2x are off when N > 1.
- The texture's width and height must stay within `GL_MAX_TEXTURE_SIZE`: the menu caps N from it, and the platform ini can cap it lower.
- The menu picture, the resume picture and the screenshots take the frame scaled down (or the 1x VRAM, as today).

### Step 4 - speed: strips on more cores (for the PSC)

- Strips are independent. gpulib's async thread already moves the GPU off the emulator's core.
- The enhanced pass of each strip can run on a worker thread with its own `psx_gpu_struct` scratch (block and span buffers). That puts the PSC's four A35 cores to work on 3x/4x.
- Done only if steps 1-3 show the CPU is the wall.

### Step 5 - menu and launcher

- The PCSX menu's "Enhanced resolution" on/off becomes "Internal resolution: 1x, 2x ... Nmax". Our menu gets the row too, under Picture.
- `pl_rearmed_cbs.gpu_neon.enhancement_enable` becomes a scale.
- `abfeatures` gets `scale` so the launcher offers it.

## The limits (honest)

- **It stays software.** The PlayStation's triangles are rasterised by the CPU (NEON on ARM, the SIMD C on x86). The cost grows with N squared: 3x is 2.25 times the work of 2x, and 4x is 4 times. The 1x pass still runs.
  - The PC and the Pi 5 have the headroom for 4x and more.
  - The Pi 4/400 probably handles 3x.
  - The PSC: 2x is the known point. 3x/4x is the owner's experiment, and step 4 is its best hope.
- **A shader cannot draw the triangles for gpu_neon.** That would be a different renderer, which is the rejected plan. What the GPU does here is everything after the rasteriser: joining the strips, filtering, scanlines, the HUD and the final scale.
- **Precision:** the gradients (u/v/rgb per pixel) are fixed point, divided by an area N*N times larger. At high N, textures may wobble. This has to be checked at 4x and 8x; the `uv_hack` adjustments may need retuning.
- **The scanout heuristic** (4 buffers, the eviction tolerances) is already fragile at 2x, as upstream's own comment says. Games that draw off-screen and copy (VRAM to VRAM) fall back to the scaled-up 1x copy, as they do today.

## Order and size

| Step | What | Size |
|---|---|---|
| 1 | N up to 1024 columns (3x for 320, 4x for 256) | one owner session |
| 2 | strips, any N, 640-wide games | one or two sessions |
| 3 | GPU assembly in plat_autobleem | a session (with 2) |
| 4 | strips on threads | only if needed |
| 5 | menu, launcher | small |

Every step:
- is built for the PSC and the Pi and tested by the owner;
- 1x and 2x must be unchanged, checked by fps before and after.
