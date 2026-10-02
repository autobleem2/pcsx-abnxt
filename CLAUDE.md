# pcsx-abnxt - CLAUDE.md

AutoBleem's next PS1 emulator: **upstream PCSX-ReARMed (notaz) with what Sony and AutoBleem added to pcsx-ab
re-implemented on top**, for the PlayStation Classic, the two Raspberry Pi builds and the Windows dev host.
Author: screemer (the repo owner). It replaces `pcsx-ab` (`github.com/autobleem2/pcsx-ab`,
`E:\Programming\pcsx-rearmed-develop` - a 2017 core with patches), which AutoBleem
(`E:\Programming\autobleem-develop`) still ships until phase 8 of the plan. This file is the project knowledge
of record; keep it current in the same commit as any change it describes. Git history has the reasoning per
change (commit messages are prose).

## State (2026-09-20)

**The port plan is complete** (the owner's call, 2026-09-20 night; the plan itself, `docs/port-plan.md`,
is deleted as finished plans are - `git show 82d77a16:docs/port-plan.md` has it): the repositories, the CMake
build for every target (psc, rpi, rpi64, pcusb, win64), the SDL2 platform, the launcher's contract
(arguments, config, exit files), the front buttons and the power daemon, the disc
change with its picker (below), the in-game menu with every launcher option. All of it verified on Windows
first; **running on the Pi 400 since 2026-09-20** (64-bit, `Autobleem/bin/emunxt/`, the launcher's
Options -> "PS1 Emulator") - Crash Bandicoot and Harvest Moon with the real BIOS, the menu, scanlines, the
pad. **What is deferred, not done - the owner tests everything later**: the plan's phase 7, the
compatibility pass (the 20 built-in games and the `title.h` titles on the console, a subset on the Pi:
boot, the first minutes, a save, FMV, CDDA, a real disc swap - Parasite Eve / RE2 / FF7; a regression that
reproduces gets a `database.c` entry or an upstream issue; the pass list goes here; also the pcsx-ab test
material on `D:\AB\Games` - CHDs with CDDA, the 30-track cue, PBPs), which is what decides which of
Sony's 131 per-title hacks are needed; **the console runs pcsx-abnxt since 2026-09-21** (its first run,
r26-56, died at the game's first frame - SIGSEGV inside SDL 2.0.12's `SDL_SetTextureScaleMode()` on an
RGB565 texture, which the GLES2 renderer wraps around a native 8888 one and 2.0.12 forgets to unwrap, fixed
in 2.0.14 - libpicofe `e487c91` dropped the call; then the video chain was redone, see "the frame's way to
the screen"; games, the menu, filters, scanlines and the smoothing verified there the same day; drop the
psc tarball over `Autobleem/bin/emunxt/` to update); and phase 8, the release - nxt as *the* emulator in `emu/`, `pcsx-ab2`
archived on GitHub - which only makes sense after that pass. Out of scope by decision: the libretro core
build, Sony's `UI_INTEGRATION`/`.sts`/the Pandora-Maemo-Caanoo platforms, GPU/SPU features beyond
upstream's; a "which disc" picker in the launcher's resume menu and the `.m3u` hand-over are follow-ups.

**The menu's look** (2026-09-20 night, `feature/menu-improvements`): the Home-button menu is drawn by
`ab_menu.c` itself (`ab_menu_draw`/`ab_menu_run` - libpicofe's `menu_entry` rows and its handler contract,
`me_loop_d`'s keys, but our screen): AutoBleem 2's launcher art (`skin/ab_background.jpg`, since
2026-09-30 the ab2.0.0 art from autobleem-design `themes/ab2.0.0/design/emu/ab_background-p5.jpg` - the
logo bottom left, a cut-corner hint bar at 466..1268 x 614..684 - before that the ab2 theme's
`AB-EvoBack.jpg`; 1280x720, decoded by **stb_image** - vendored `frontend/ab/stb_image.h`,
JPEG+PNG only - and scaled to cover the canvas by `ab_ui_background`; plain navy without the file), the
game's name and id top left with the selected row's help or the last message under them, the rows on a
translucent panel on the right (a value row shows its value with `< >` arrows when selected - Filter
became one, `mee_cust_h` over `plat_target.hwfilters`), the pad hints and the build (`REV`, the CPU
engine, the GPU, `__DATE__`) on the art's bar. Everything is a 1280x720 design scaled by the canvas'
height, in the ab_ui font. **The look is data** (2026-09-30, the owner's call, EMU-16): the colours, the
panels' alpha and the background's and font's file names are `skin/skin.cfg` (`docs/skin.md` has the keys),
read once by `ab_ui_skin()`, ab2.0.0's values built in for a missing file or a bad key - so the menu can
follow the launcher's theme once the launcher writes that file (a later step); the layout stays code.
**The ab2.0.0 layout** (EMU-16, the named `AB_PANEL_*`/`AB_ROW_*` constants at the top of the menu screen's
code in `ab_menu.c`): the rows' panel at x 32..572, y 24..596 (18 px above the art's hint bar, clear of the
logo) with the v02b cut corners (top right and bottom left, 16 px) and a 2 px accent rim, drawn by
`ab_ui_cut_panel` (one `ab_ui_fill` span per row, rim and inside never overlapping); the selected row the
same shape (cut 8) in the wash colour with a magenta rim; section headings with a 1 px accent rule to the
panel's right. The skin's `text_scale` (110 for Red Hat Text) keeps the glyphs as large as Selawik's were
at the design's sizes. Rows are one line: a name that would run into its value is drawn smaller, down to
70 % (upstream's long PCSX-menu names in fr/oc/pt_BR/de). The art's hint bar carries only the hints -
the launcher's pad glyphs (`ab_ui_pad_glyph`: a grey disc with a blue Cross or a red Circle, the skin's
`hint_*` colours), centred on the bar's middle line from x 490; the build's two lines moved out of the bar,
right-aligned at x 1248, y 568/590, and the right-hand column's help/message text stops above them. The
disc picker's panel, the message box, About and the snapshot's frame are the same cut-corner panel
(`ab_panel`; the snapshot clipped to the frame's cut), the L1/R1 chips the selected row's shape, About's
rule a 1 px accent line. **The paused game's frame is not shown any more**: `menu_leave_emu()`'s paste
of `pl_vout_buf` at `last_vout_w/h` was garbage on the console whenever the GPU rendered at another size
than it reported (the 2x enhancement, say) - `ab_menu_prepare_bg()` covers it at every menu entry and
gives libpicofe's `g_menubg_*` a darkened copy of the same art, which is what the PCSX menu beneath and
its pages draw over, in upstream's own 8x10 font. `plat_sdl2.c`'s `resize_cb` re-allocates the
`g_menubg_*` buffers with the canvas (F11 on a PC used to read past them). The picker and the message
screens draw on the same screen (`ab_screen_begin`, the discs on a panel, the hints on the bar).
`make_rpi*.sh`'s dist step ships `skin/` + `lang/` like `ci/build.sh`'s. **`ab_ui` looks in the run
directory first** (`data_path`): the launch scripts copy the binary to `/tmp/pcsx` and link `skin/`,
`lang/`, `fonts/` into `/tmp/runpcsx`, so `emu_make_data_path` (exe-relative) found nothing on the Pi.
**The menu button** (`ab_filter_action`, hooked into `update_input()`): a press opens the menu on release
and a **2 s hold is Reset** (the HUD says "HOLD TO EXIT" from 0.5 s; the Exit row's help says so). On every
platform since 2026-09-24 - the console used to open the menu at once, leaving Reset to the front button,
but its pad has no Home and Select+Start held is the way out players reach for (`in_sdl2gc` turns
Select+Start into Home on a pad whose mapping has no `guide`). `tools/win_drive.ps1` holds a key with `name:ms`.

**The disc picker and the emulator's own language** (2026-09-20, phase 5 complete): the Open button and
the menu's "Change disc" open `ab_disc_screen()` (`ab_menu.c`) over the menu's screen - the set's discs in
a row (drawn in code, `ab_ui_disc`: the one in the drive in AutoBleem's cyan, the focused one bright with
a ring, the others dimmed), "Disc n" under each, Cross/Circle hints; Left/Right, Cross puts the focused
disc in through the lid (`ab_disc_insert`), Circle backs out, the focus starts on the *next* disc as
Sony's did (Open, Cross is the common case). A single-disc game or a press in the 22 s grace gets a
message screen with an OK. **The text is the launcher's language, not Sony's 13 PNG sets**: the
launcher starts pcsx-abnxt with `-language <Name>` (config.ini's `language`, the name of its own lang
file - only nxt gets it, the classic pcsx-ab would take it for a file) and `lang/<Name>.txt` next to the
emulator (`frontend/ab/lang/`, all 17 of the launcher's languages, its `English text=Translated text`
format) has the seven strings; `ab_ui.c` rasterises them with **stb_truetype** (vendored,
`frontend/ab/stb_truetype.h`, public domain - no new library on any platform) straight into the menu's
RGB565 canvas from `skin/ui.ttf` (**Red Hat Text Medium** since 2026-09-30, EMU-16 - the ab2.0.0 theme's
`font/RedHatText-Medium.ttf`, 52 KB, OFL 1.1, its licence shipped next to it as `skin/ui-OFL.txt`; it must
be a **static** instance: stb_truetype ignores `gvar`, so the variable `RedHatText[wght].ttf` would draw
its default weight. Its `px` box is the hhea ascent-descent, 1.32 em against Selawik's 1.20, so the same
`px` draws ~9 % smaller glyphs - the menu's sizes are scaled by the skin's `text_scale`, the HUD's lines by
its `hud_scale` (both 110; the HUD's in `ab_hud_line_update`, only when a line's text changed and it is
rendered again, capped so the strip fits its 64 px slot). No Cyrillic/Greek/kana, as
Selawik had none: a language that needs them names its own `|@font|`. Until then Selawik Regular 1.01,
Light until 2026-09-28, too thin over a game for the HUD, which draws with it too) or the font a language file names with `|@font|` (Chinese: the launcher's
`NotoSansSC-Regular.otf`, found in `fonts/`, which the launch scripts link to the launcher's fonts
folder). No font = English through libpicofe's 8x8 font. `ci/build.sh`'s `dist()`, `make_packages.sh`
and `make_win.sh` ship `skin/ui.ttf` + `lang/` with the emulator. **Do not call `CheckCdrom()` after
`LidInterrupt()`**: the core's lid sequence runs it itself on the close, and a second one mid-sequence
killed the game. A swap while a game is loading kills it as on hardware (Crash during its boot: an
invalid load under lightrec, a black screen under the interpreter); at its title it goes on with the new
disc. Keyboard: F9 = Open, F10 = Reset (the console's `eject`/`reset` keys are bound too).
`PLAT_SDL2_SHOT_MS` sets the capture interval (a menu screen is gone in 5 s).

**What the first Pi day found and fixed** (r26-26..32), each a rule from now on:
- every launcher option must reach nxt: the game editor's `pcsx.cfg` keys were audited against the
  config table - all the same, except upstream's versioned rename **`frameskip3` -> `frameskip4`**, read and
  written as an alias now (`CE_INTVAL_N("frameskip3", ...)` ahead of it). Values are parsed as **hex**
  (`strtoul(..., 16)`): the launcher writes 0/1 flags decimal and levels hex, both fine.
- **`Config.SlowBoot` defaults to 1** (`emu_set_default_config`): upstream's 0 skips the BIOS logos, and the
  launcher writes no line for "shown".
- a pcsx-ab-era cfg says `plat_target.vout_fullscreen = 0`: on the console and the Pi the frontend forces
  fullscreen (`check_fullscreen`) - it used to drop to a 1280x720 window inside 1080p and the pointer came
  back. The pointer itself is hidden by relative mouse mode (KMSDRM ignores `SDL_ShowCursor`).
- **the frame's way to the screen** (libpicofe `plat_sdl2_present`, the owner's pcsx-ab chain redone on
  the GPU, 2026-09-21 after the console showed the first version's uneven scanlines): the RGB565 frame is
  scaled **once** into the backbuffer with the filter (`-filter 0..6`, an index into `ab_filter_names` since 2026-09-29 - the launcher's game
  editor offers all seven; was 0..2: Off = nearest, Linear = bilinear,
  Sharp = whole-multiple prescale in a native 8888 render target then bilinear for the remainder - at 720p
  a 240-line game's Sharp equals Off, inherent), and the **scanlines go over the scaled picture**, never
  scaled again: a property of the screen like a CRT's, 240 lines over the picture's height whatever the
  game's mode (3 px each at 720p, 4/5 alternating at 1080p), one ARGB overlay texture made on the CPU when
  size/thickness/level change and blended in one copy (Scanlines 1-3 = thickness/4 of a line, at least 1 px,
  a pixel of picture kept - 1/2/2 px at 720p; brightness = what shows through). Not fill rects: back when
  the console shipped SDL 2.0.12, its GLES2 drew those 1 px high whatever was asked - not retested since
  the move to 2.0.14, so the blended-overlay approach stays. The menu never has them (`dst == NULL`), FMV does.
  `pl_scanlines_by_plat` keeps plugin_lib's own row-darkening off (a C `bgr555_to_rgb565_b` exists for the
  builds without NEON32 all the same). The launcher's GFX Filter sends 0/1. A game with
  `gpu_neon.enhancement_enable = 1` (Crash's PC-era cfg) is 2x before any of this, which is why the owner
  saw no difference between Off and Linear on it. What is outside the picture (a 4:3 game's side bands) is
  the backbuffer, free for anything drawn before the present.
- **Smoothing** (the menu row under Filter; upstream's `soft_filter`, `pcsx.cfg` 0-4, also the PCSX menu's
  "Software Filter"): None / Scale2x / Eagle2x / HQ2x / HQ3x, `frontend/ab/ab_scaler.c` hooked into
  `plugin_lib.c`'s blit - the BGR555 frame converted to RGB565 (NEON on 32-bit ARM) into a scratch buffer,
  scaled 2x/3x into the frame buffer, then the chain above. Scale2x/Eagle2x are libpicofe's NEON asm on
  32-bit ARM and C in `ab_scaler.c` elsewhere; HQ2x/HQ3x are grom358's **hqx** (LGPL 2.1, vendored in
  `frontend/ab/hqx/`, generated for RGB565 in and out by a script - the pattern switches verbatim, the driver
  loops and the 16M-entry RGB->YUV table replaced by two 64K tables; regenerate rather than patch). The
  scaled frame must fit `PL_VOUT_MAX_W/H` = 1600x1024 (`plugin_lib.h`; the platform's frame buffers are
  that size): 320x240 and 512x240 get all five, 320x480/640x480 the 2x ones, HQ3x there = "filter
  unavailable" as upstream's message goes; 24-bit frames are never scaled. `pl_update_layer_size`'s 4:3 rule
  reasons from the PSX line count (`h / pl_vout_scale_h`), or a 3x frame looked 480i to it. **On the console
  HQ2x/HQ3x run at 30 fps** (the owner's call: left as is, off by default; a scaler thread on a spare core
  is the follow-up if it ever matters); Scale2x/Eagle2x cost nothing visible. xBRZ was ruled out: GPL-3.
- SDL falls back to the **offscreen** driver when the DRM master is not free yet (the launcher's window,
  or the previous emulator, for a few seconds): `plat_sdl2_init` retries video init for up to 6 s
  instead of rendering into nothing.
- `Config.PluginsDir` = `./plugins` when it exists, like `bios/` - the launch scripts put it next to `.pcsx`
  (upstream's exe-relative dir was `/tmp/plugins`, empty).
- the AutoBleem menu has Controller 1/2 (standard/analog/guns/none), Scanlines + brightness and Screen
  (4:3 / 16:9 over `g_scaler`) - what pcsx-ab's Sony menu offered.
- **the debug driver** (2026-09-22, `frontend/ab/ab_debug.c`, the shape of the launcher's own
  `DebugDriver`): `AB_DEBUG_PORT=<port>` starts a TCP line server on the loopback that pushes keys into
  SDL's event queue (so they take the real path: in_sdl2 -> the binds -> the menu or the game) and hands
  frames back - `press/down/up <key>`, `wait`, `shot <file.bmp>` (the readback happens in the present that
  follows the request, `plat_sdl2_shot_*` in libpicofe; a menu presents only when it redraws, so the shot
  pushes an expose first - `PBTN_RDRAW`), `screen` (boot/game/menu/pcsx/disc/message, set where our menus
  draw), `row` (the highlighted row's name - libpicofe's `menu_sel_name`, kept by every `me_draw`),
  `status`, `frames`, `fps` (2026-09-29, a permanent command for any client, the launcher's too: `ok fps=F
  vsync=V cpu=C` - the last second's frames the game drew, emulated vsyncs (under 60/50 = slower than the
  console) and CPU load; fps/cpu are -1 unless the HUD counts them, pcsx.cfg `g_opts = 3`), `quit`.
  Measured with it on the PSC (Soul Reaver, frameskip off): the game draws 30 of 60 vsyncs at every setting -
  its own rate, not the emulator's - and 2x resolution takes the load from ~34% to ~85-90%, filters, Scale2x
  and scanlines adding a few points; the GPU thread made no measurable difference there. `tools/emu_drive.py start|run|stop|sheet` is the client: `run "press escape;
  wait_screen menu; enter PCSX menu; enter Options; enter [Display]; shot d.png"` - `enter`/`select` walk
  by row name instead of counting keypresses, and a crash comes back as the connection dying with the tail
  of `build_win/run/err.txt`. Nothing of it runs without the variable. On a Pi or the console: start the
  emulator with `AB_DEBUG_PORT` and `ssh -L`, then `--host/--port`.
- **debugging a display one cannot see**: `PLAT_SDL2_SHOT=/tmp/shot%d.bmp` saves the presented frame every
  5 s (the emulator's own screenshot is the raw PSX frame); `AB_err.txt` has one line per video mode
  (`video mode: 1024x480 (psx 512x240)`) and per loaded config (`autobleem: game config: filter=... boot
  logo=... scanlines=...`), and the cursor calls. On the Pi: stop `autobleem.service`, run
  `Autobleem/rc/launch.sh <ss> <cue> 2 4 <game> 0 <aspect> <filter> NA pcsx-abnxt` under `sudo` with the
  env, then start the service again. `tools/win_drive.ps1 -AttachPid` drives an emulator started under gdb.
- **a crash on the console** (how r26-56's was found): the launcher's `rc/launch.sh` leaves the emulator's
  stdout+stderr in `System/Logs/pcsx.log` with the exit status (139 = SIGSEGV) and `launch.log` says
  `(core dumped)`, but the core goes to the console's `systemd-coredump` pipe, there is no `ldd`, and
  `/tmp/runpcsx` is wiped by the next launch. A debug `launch.sh` on the stick (root: `echo /tmp/core.%e.%p
  > /proc/sys/kernel/core_pattern` before the run, restored after; `cp /tmp/core.* System/Logs/pcsx-core`;
  `cp -L` of `/lib/{ld-linux-armhf.so.3,libc.so.6,...}`, `/tmp/lib/libSDL2-2.0.so.0` and `/usr/lib/lib{EGL,
  GLESv2*,PVR*,srv_um,wayland*,gbm,drm}*` into `System/Logs/psc-libs/`) brings an 83 MB core home, and
  `arm-linux-gnueabihf-gdb` (SysGCC's, `C:\SysGCC\raspberry\bin`) reads it with `set sysroot .`,
  `set solib-search-path psc-libs`, `file <unstripped>`, `core-file pcsx-core`, `bt` - the unstripped
  `build_psc/pcsx-ab` stays on the build server (`~/pcsx-abnxt/build_psc/`; check the GNU build id against
  the shipped one with `readelf -n`). **The console runs the SDL 2.0.14 of AutoBleem's `libs.tar.gz`**
  (unpacked to `/tmp/lib` at boot, `LD_LIBRARY_PATH` inherited from the launcher; the firmware's own
  `/usr/lib` SDL is 2.0.4) - the same build the emulator links against in the Docker image, so SDL
  functions newer than 2.0.14 do not exist there and 2.0.14's own bugs do. Kernel 4.4.22 armv7l, Weston,
  the PowerVR GLES2 driver (`libGLESv2_PVR_MESA.so`); "PVR:(Error): glBufferSubData: No memory for object
  data" in the log is the driver's noise at the first present, not a failure.

| | |
|---|---|
| Repository | `github.com/autobleem2/pcsx-abnxt`, a **public GitHub fork** of `notaz/pcsx_rearmed` (GPL-2; a fork of a public repo cannot be private - the owner's call, 2026-09-20) |
| Base | upstream tag **`r26`** (2026-03-29, `56fef013`), the latest stable; `develop` starts there |
| Branches | `master` mirrors upstream master (fast-forward only, never committed to); `develop` is ours; `feature/<slug>` off `develop`, merged `--no-ff` (gitflow, as in every AutoBleem repo); `upstream` remote = notaz |
| libpicofe | submodule `frontend/libpicofe` -> **`github.com/autobleem2/libpicofe`** (our fork of notaz's), branch `develop`: r26's commit plus our SDL2 files (`plat_sdl2.*`, `in_sdl2.*`, `in_sdl2gc.*`); `upstream` remote there too. The other submodules (`deps/libchdr`, `lightrec`, `lightning`, `libretro-common`, `mman`, `frontend/warm`) are upstream's, untouched |
| Build | `CMakeLists.txt`: upstream's `configure`/`Makefile` as CMake options (`PCSXAB_*`), the plugins, libchdr/lightrec/lightning/mman compiled from `deps/`; upstream's own build files stay untouched. `PCSXAB_PLATFORM=sdl2` (ours, the default), `sdl` (upstream's SDL 1.2 frontend, needs sdl12-compat on a PC) or `headless` |
| Windows | `./make_win.sh` -> `build_win/pcsx-ab.exe`: **lightrec + C-SIMD gpu_neon, plays games** - Crash Bandicoot's intro in a 1280x720 window, Esc opens the menu, `tools/emu_drive.py` drives it over the debug driver's socket (see "the debug driver"); `tools/win_drive.ps1` is the older way, keys posted to the window |
| Pi 32-bit / 64-bit | `./make_rpi.sh`, `./make_rpi64.sh` -> `build_rpi*/dist/`: Ari64 ARM / ARM64 dynarec, NEON asm / C-SIMD GPU, the SDL2 platform - **the 64-bit build runs on the Pi 400** (2026-09-20), 32-bit built, unrun |
| PlayStation Classic | `ci/build.sh psc` in the Docker image (gcc-6, `/opt/psc`, SDL 2.0.14): builds and links, GLIBC <= 2.24, no RPATH, ARM dynarec + NEON - **runs on the console since 2026-09-21** (`build_psc/dist/` on the PC holds the last fetch; the console's SDL is 2.0.14, see "a crash on the console"); `make_psc.sh` is the Sony-toolchain path over ssh, untested here |
| Local checkout | `E:\Programming\pcsx-abnxt` |
| Packages | `tools/make_packages.sh` -> `dist/packages/pcsx-abnxt-<git describe>-{psc,rpi-armhf,rpi-arm64}.tar.gz`, `-win64.zip` (from `build_win_rel`, a Release configure of the same tree) and a manifest json; the Linux dists come from the build server (`ssh psc-build`, `~/pcsx-abnxt` an rsync copy without `.git`: `AB_GIT_DESCRIBE=$(git describe)` in the environment is what CMake bakes into `REV` there, through `docker/run.sh`'s `AB_*` pass-through - without it the menu's build line says "(no version)"); published with autobleem-develop's `tools/repo_publish.sh pcsx <version> dist/packages/*` to **`https://autobleem.retromenele.pl/emu/pcsx-abnxt/`** (`latest.json`, the newest kept; first publish `r26-20-gb9801962`, 2026-09-20, marked a development build; **`r26-alpha1`, 2026-09-21, the first alpha** - an annotated tag on `develop`, so `git describe` reads `r26-alpha1-N-g...` from there: the upstream base stays visible). **The build server's clock is ~5 min behind this PC's**: rsynced files just edited here have mtimes in its future and ninja loops on them ("manifest still dirty") - `find . -newermt now -exec touch {} +` in `~/pcsx-abnxt` before a build |

**The SDL2 platform** (`frontend/plat_sdl2.c` over libpicofe's `plat_sdl2`/`in_sdl2`/`in_sdl2gc`): one window
and one accelerated renderer everywhere; the GPU plugin draws into a shadow RGB565 buffer that
`plat_gvideo_flip()` uploads to a streaming texture and presents into the layer plugin_lib works out
(`g_layer_*`; `plat_target.hwfilter` 0 = linear, 1 = nearest); the menu draws into a buffer the size of the
window (the 2x font from 640x480 up). Fullscreen-desktop on the ARM targets, a resizable 1280x720 window on a
PC (`plat_target.vout_fullscreen` / F11 toggles). Keys are SDL scancodes named in lower case ("escape", "f1",
"eject", "reset"); pads are `sdl2gc:pad N` with fixed PlayStation button names, Select+Start = Home on a pad
without a Guide button, player 1's sticks wired to `in_adev[]` at probe time. `PCSXAB_GLES` (libpicofe's own
EGL output) is off on every target.

**`frontend/ab/`** is ours. `ab_config`: the launch script's arguments taken out of argv (`ab_args_take`),
`Bios = SET_BY_PCSX` -> `Config.Bios[US/EU] = romw.bin`, `[JP] = romJP.bin` (upstream picks by the disc's
region, HLE without the file), `card2.mcd` -> `none`, `-filter`/`-ratio` -> hwfilter and `g_scaler`
(`ab_config_loaded`, hooked at the end of `menu_load_config`); stdout unbuffered. `ab_session`: the exit
files (`ab_session_exit`, hooked after main()'s loop) - `sstates/<label>-<id>.000`,
`screenshots/<label>-<id>.png`, `lastcdimg.txt`, and last `filename.txt`, named by the disc in the drive.
`ab_buttons`: our emulator actions (`SACTION_AB_RESET` = "RESET button", `SACTION_AB_CD_CHANGE` = "CD
Change button" - the pcsx.cfg bind names, on the `reset`/`eject` keys the console's front buttons send -
`SACTION_AB_POWER_OFF`), handled in `do_emu_action`'s default branch, and
`ab_frame_tick()` from `pl_frame_limit`. **Rule**: anything that touches the emulator's state runs as an
action, between CPU slices - `SaveState()` from inside a slice froze the game. **Every way out leaves the
game as it is at that moment** (2026-09-24, the owner's call): the menu's Exit, the window's close, the
menu button held, Reset, Power and overheating all end main()'s loop, and `ab_session_exit()` saves the
live state after it, between two slices. Until then Reset/Power (and so the hold) left an autosave ring's
oldest snapshot, ~10 s back, as Sony's firmware did - the ring (`ab_autosave`, a memory `SaveState()` every
2 s) is gone with it. Two things a way out still waits for (`leave()` in `ab_buttons.c`): a memory-card
write in the last 2 s (`ab_memcard`; "SAVING..." on the HUD - the state and the card file must agree),
and two frames presented without the HUD, which is printed into the frame the resume picture is taken from.
**A quit while a menu is open** (the window's close, or SIGTERM, which SDL turns into SDL_QUIT - a stop from
the launcher, a power-off, a test kit; EMU-18, 2026-10-02) used to wait until the player left the menu:
`plat_autobleem.c` sets libpicofe's `in_set_menu_quit_check()`, so a menu's wait for a key ends and
`in_menu_wait()` answers Back until every menu has unwound, and main()'s loop ends the run as the menu's Exit
does (the same resume point).
`ab_console`: the power daemon's `prepare_suspend` and `cpu_temp`/`temp_limit` watchers (inotify threads,
Linux only, ending at once without the files). `ab_disc`: the disc set (multi-disc PBP, an `.m3u`, or the
folder's images of the same kind) and the Open button through the core's lid, refused for 22 s after the
start; one press = the next disc, with a HUD line. `SaveMcd()` fsyncs and tells `ab_memcard`. `ab_menu.c`:
the in-game menu (Resume, Quick save/load = slot 2, Change disc, Filter, Smoothing, Screen, Scanlines,
the controllers, PCSX menu = upstream's whole menu beneath, Save settings for this game = the game's own
`pcsx.custom.cfg` - see "A game's config", Exit) on its own screen (see "The menu's look"), `#include`d into `frontend/menu.c` like
libpicofe's menu.c because the menu machinery is static there. `ab_debug.c`: the debug driver (see "the debug driver"; `AB_DEBUG_PORT` only). `ab_scaler.c` + `hqx/`: the smoothing
scalers (see "Smoothing"; `tools/vendor_hqx.py` regenerates `hqx/hq2x.c`/`hq3x.c` from a clone of
grom358/hqx). Player 2's sticks: `in_adev[4]` ([2]/[3]), `update_analogs()` over both players.

Upstream files edited so far (the whole list - keep it that way): `frontend/main.c` (`path_is_absolute()`
for `C:\` paths - a candidate for an upstream PR; the `ab_*` hooks: the arguments, the exit, the action
default; `PCSX_MEMCARD_COUNT` instead of a fixed nine cards, 2 here), `frontend/main.h` (the macro's
default, our three `SACTION_AB_*` values), `frontend/menu.c` (the `ab_config_loaded` hook, two action
names, the game's own config - its name, load order, the merging save and binary files, see "A game's
config" -, the save leaving out the HUD bits `AB_PERF_OVERLAY` turned on, `men_soft_filter`'s five names on every platform; `menu_init` keeps the "Video output mode" row off
when the platform has no `vout_methods` - ours has none, and upstream's `MENU_SHOW_VOUTMODE` default of 1
re-enabled the row with a NULL name list, which crashed the PCSX menu's [Display] page on every target,
found on the console with r26-alpha1 - a PR candidate), `frontend/menu.h` (`SOFT_FILTER_HQ2X/HQ3X`),
`frontend/plugin_lib.c` (`ab_frame_tick()`; the analog tables at 4 and `update_analogs()` over both
players; `pl_scanlines_by_plat`; the smoothing: `ab_soft_scale_factor()` in `pl_vout_set_mode`,
`ab_soft_blit()` in the flip in place of the `HAVE_NEON32` scalers, `resolution_ok()` against
`PL_VOUT_MAX_*`, the 4:3 layer rule from the PSX line count), `frontend/plugin_lib.h` (the same tables,
`pl_scanlines_by_plat`, `PL_VOUT_MAX_W/H`), `frontend/menu.c` also `#include`s `ab/ab_menu.c` and
runs `ab_menu_loop_d()` - and no "you have no BIOS" screen (`menu_bios_warn` is gone, 2026-09-24, the
owner's call: the menu's header line says HLE or BIOS), `libpcsxcore/sio.c` (`ab_memcard_written()` + fsync in `SaveMcd`),
`libpcsxcore/misc.c` (`SaveState`/`LoadState` renamed `SaveStateNative`/`LoadStateNative`, `state_mark()`
between the sections - see "The save-state layout"), `libpcsxcore/cdrom.c` and `psxcounters.c` (a block
of ours at the end of each), `CMakeLists.txt`/`Makefile` (`state_sony.c`), `.gitignore` (`/tools/*` so a file of ours under it can be tracked). Everything
else Windows-specific is a shim: `frontend/win32/` (the host layer, `<dirent.h>` with `d_type`/`scandir`,
`win32_compat.h` force-included by CMake) and `NO_DYLIB` (upstream's own Windows recipe).

**Known**: a save state loaded within the first seconds of a **HLE** boot (`-load 1` at start, or F2 at one
second) leaves the game spinning in the HLE BIOS - with lightrec and with the interpreter alike; at 25 s the
same state loads fine. Upstream's HLE keeps state outside RAM, so a state taken later cannot be put into a
freshly booting HLE. The console and the Pi run real BIOS files, where this does not arise; a PC without one
cannot test the resume path.

**A game's config** (2026-09-24, the owner's design, `frontend/ab/ab_config.h` has it in full): one source
at a time. `pcsx.cfg` is AutoBleem's (the launcher's game editor; `launch.sh` puts it in `.pcsx/`);
`.pcsx/pcsx.custom.cfg` is the game's own, and **every save in the menus writes it** - "Save settings for
this game" in ours and in the PCSX menu's Options and Controls pages (upstream's "Save global config" and
"Save cfg for loaded game" are gone under PSCLASSIC; `make_cfg_fname(1)` is the fixed name, not the per-disc
`cfg/<label>-<id>.cfg`). While it exists the launcher shows the game's emulator settings locked, "Unlock"
there deletes it. At the game's start `menu_load_cd_image` loads `pcsx.cfg`, then the custom file over it
(a key it lacks keeps AutoBleem's value); a key it has beats `-filter`/`-ratio` (`ab_config_loaded`). A
save keeps the lines of keys this build does not know (pcsx-ab's: the two emulators share the file), once
each and without a CR, and writes `Bios = SET_BY_PCSX` back while the BIOS is the one it picked
(`ab_bios_set_by_pcsx`). Found on the way: upstream's first-run BIOS autoselect took every menu opening
under AutoBleem for a first run (our pcsx.cfg has no `config_save_counter`) and put `romJP.bin` in - gone
under PSCLASSIC; and the config was written in text mode, so on Windows (CRLF) `menu_load_config` never
read it back - binary both ways now, `parse_str_val` drops a CR. pcsx-ab (pcsx-ab2) does the same.

**The save-state layout is pcsx-ab's** (2026-09-24, `libpcsxcore/state_sony.c`, the owner's call): the
launcher lets the player switch between the two emulators, so a resume point either wrote has to be one the
other continues from, and the layout they share is the one pcsx-ab (Sony's build) has always written.
`SaveState()` runs upstream's `SaveStateNative()` into memory with `state_mark()` at each section and writes
it translated; `LoadState()` reads the file, rebuilds upstream's stream and runs `LoadStateNative()` over
it - so upstream's own save/load code is untouched. What differs (all in the file's header comment): Sony's
GPU header has two more words, its SPU blob three more fields (`SPUInfo`, `volume`, `reverb` - a pointer, so
12 bytes on a 32-bit build and 16 on a 64-bit one, both read), event slots 6/13 are its GPUBUSY/CDRPLAY
(our SPU_IRQ/IRQ10; it plays CD audio on CDRPLAY, we on CDREAD), the CD-ROM struct has the same offsets
but some fields mean something else (`cdrStateToSony`/`FromSony` at the end of `cdrom.c`), the MDEC's
pointers count from psxM + 1 MB, its loader divides by the base counter's target, it takes the GPU's busy
bit from its copy of GPUSTAT in the hardware registers and only its GPU DMA event sets it again (we time
busy with `gpuIdleAfter` and leave the copy as it happens to be - a state of ours saved in a busy moment
kept pcsx-ab's GPU busy for good, Crash sat on its loading screen; written idle now unless a GPU DMA is
running), and the stream ends in its disc-change state where we save the pads. After that comes our extension (`ABNXTEX1`: the registers,
CD-ROM, counters, MDEC, pads and I_STAT as they were, and the SPU fields' width), which pcsx-ab never reads -
so a state of ours comes back exactly. A file in upstream's layout (what nxt wrote before, RetroArch's)
still loads as it is. An HLE-BIOS state cannot cross over (each emulator keeps its own HLE data in the BIOS
area) and is refused either way - pcsx-ab's `LoadState` got the same check. The `_Static_assert`s in
`state_sony.c` and `cdrom.c` fail the build if an upstream merge changes a section's size, and a size
`SaveState()` does not expect makes it write upstream's layout rather than none. Verified on Windows
2026-09-24: the console's own 2018 states (WipEout XL, Resident Evil 2, Tomb Raider II mid-read) continue
here, and ours walk as pcsx-ab's layout section by section. pcsx-ab loading ours, on the Pi 400: Crash
saved by nxt mid-read on its loading screen goes on into the level in pcsx-ab (run headless with
`SDL_VIDEODRIVER=offscreen`, looked at by having gdb call its `SaveState()` and drawing the display area
from the state's VRAM - its GPU plugin leaves the state's own picture black). The Windows pcsx-ab dev build
is no help there: it crashes a moment after loading any state, its own included.

## What the launcher hands over (2026-09-24, AutoBleem's quiet-stick plan)

AutoBleem writes to the stick only when the user's state changes, and the emulator is asked to help
through the environment - an older launcher sets none of these and an older emulator ignores them. The
launcher reads the **`abfeatures`** file next to the binary (`frontend/ab/abfeatures`; the getters in `frontend/ab/ab_config.h`, copied by every packaging script:
`ci/build.sh`, `make_rpi*.sh`, `make_win.sh`) and sets only what it lists:

- `AB_EXIT_DIR` (`exitdir`): the resume point of the way out - `sstates/<name>.000`,
  `screenshots/<name>.png`, `filename.txt` (last: "ended cleanly"), `lastcdimg.txt` - goes there, in
  RAM, in the `.pcsx` layout; the launcher copies it to the stick only when the player keeps a slot.
- `AB_MEMCARD_DIR` (`memcarddir`): the game's memory-card set (`Games/!MemCards/<set>`), played where it
  is - `Config.Mcd1` points into it - instead of being copied in and out around the run.
- `AB_LOAD_STATE` (`loadstate`): the kept slot to resume from, loaded like `-loadf` (and `-load` is then
  ignored) instead of the launcher copying it to slot 0 first.

- `AB_PERF_OVERLAY=1` (`perfoverlay`, 2026-09-29): the launcher's Options -> "Show performance" is on, so
  the HUD shows FPS and CPU too (`g_opts |= OPT_SHOWFPS | OPT_SHOWCPU` in `ab_config_loaded`) - for the run
  only: the bits it turned on are left out of every config save (`ab_perf_forced_opts()`, asked by
  `menu_write_config`), so the game's `pcsx.custom.cfg` never keeps them.
- `AB_SCALER=0..4` (`scaler`, 2026-09-29): the launcher's Options -> "Emulator screen scaling" as `g_scaler`
  (SCALE_1_1, 2_2, 4_3, 4_3v2, FULLSCREEN), set in `ab_config_loaded` ahead of `-ratio`; a game whose
  `pcsx.custom.cfg` has its own `g_scaler3` keeps that. Unset or out of range: `-ratio` as before.

Only the cards in use are created (no `card2.mcd`, which is "none"). Not yet run on a console.

**Dithering** (2026-09-29): the in-game menu's Picture section has a Dithering row - Off / On / Always,
upstream's `pl_rearmed_cbs.dithering` (0 = none, 1 = where the game's draw command asks for it, the
PlayStation's own behaviour and the default, 2 = on every shaded or modulated primitive; gpu_neon's
`allow_dithering`/`force_dithering`, the PCSX menu's "GPU dithering" is the same value). **The launcher sets
it per game through `pcsx.cfg`: `dithering2 = 0|1|2`** (upstream's versioned key name; no line = 1). It is a
config key, not an environment variable, so there is no `abfeatures` line: every nxt build reads it, and the
classic pcsx-ab ignores the key (its own dither lives in `gpu_peops.iUseDither`). A game's own
`pcsx.custom.cfg` with the key beats the launcher's value, as for every key. The row edits a copy
(`ab_rows_take`/`ab_rows_commit`, like Scaling) that is written back before a save and when the menu closes,
only if it was moved; the GPU takes it when the game goes on (`plugin_call_rearmed_cbs`). The config log line
says `dithering=N`.

Also: `lastcdimg.txt` is not rewritten at start when it already names the disc, and stdout is
line-buffered off Windows (`ab_args_take`).

## What this is built from - read first

- The port plan (the analysis, the decisions, the eight phases) is in the git history: `git show
  82d77a16:docs/port-plan.md`. The decisions it made are recorded in this file.
- **`docs/reference/features.md`** - every feature Sony and AutoBleem added to pcsx-ab, with line pointers
  into **`docs/reference/pcsx-ab-delta-2017.patch`** (pcsx-ab against its real upstream base, `bebe989b` of
  2017-10-17, whitespace-normalised) and **`libpicofe-delta-2015.patch`** (its libpicofe against `21604a0`).
  Port a feature from the patch and the inventory, never from the old tree.
- The AutoBleem side of the contract: `autobleem-develop`'s `CLAUDE.md`, `src/code/core/services/launch.cpp`,
  `resume_point.h`, `game_settings.cpp`, `src/resources/pcsx.cfg`, `payload/Autobleem/rc/launch.sh` and
  `payload_rpi/Autobleem/rc/launch.sh`.

## Decisions (made by the owner - do not re-ask)

- **Re-implement, do not re-apply.** The old delta is 2017-core patching; upstream r26 already has CD lid
  emulation, `SlowBoot`, the fast boot, KSEG1 decoding, CHD, an aarch64 dynarec, lightrec, a C-SIMD NEON GPU
  and a per-serial hack database. Each feature is written again against what upstream has now.
- **Per-title hacks are not ported** (131 serials, `isTitleName()` at 463 sites). Phase 7 tests the titles;
  a reproduced regression gets a `libpcsxcore/database.c` entry (or a `Config.hacks` flag), never an
  `isTitleName()` in the core. **Except their configuration layer, behind a switch** (2026-09-21): Sony's
  `config_change()` (136 of the sites, `frontend/menu.c` in the fork) applied SPU interpolation 2/3, SPU
  thread off, interlace off, NTSC, a P.E.Op.S. swap with Sony-only flags and `iTempo` over pcsx.cfg at
  every start, keyed on the **ISO file name** (`setCdromId()` overwrote `CdromId` with it - the console's
  files were serials, a USB game's name is not). `frontend/ab/ab_hacks.c` has the four overrides that
  exist upstream, keyed on the **disc's real `CdromId`**, in a table `tools/gen_sony_hacks.py` generates
  from the reference patch (`ab_hacks_table.h`, 66 serials; the peops/`iTempo` ones listed in its header,
  not ported), applied in `ab_config_loaded(1)` **only with `-sonyhacks`** and logged per override
  (`autobleem: sony hack SLUS00708 (...): spu interpolation gaussian`). A lever for the compatibility
  pass - most of it is probably obsolete against upstream's SPU and region detection - not a default.
- **Layout rule**: our behaviour lives in new files - `frontend/ab/` and `frontend/plat_sdl2.c` (+ libpicofe's
  SDL2 files in our fork) - and upstream files get hooks only: a call, an enum value, a config entry. No
  `#ifdef PSC` in `cdrom.c`. A platform difference is a runtime check or a CMake option, never a second copy
  of a file. This is what keeps `git merge upstream/master` cheap; merge at upstream release tags.
- **We are a platform of upstream's, `psclassic`** (2026-09-21, the owner's ask, a dry run that held): the
  hooks in upstream files that are AutoBleem's own (the launch arguments, the exit files, the actions, the
  memory-card hook, our menu, `-dotdir`'s paths) sit under **`#ifdef PSCLASSIC`**, the way notaz's
  ports sit under `PANDORA`/`MAEMO`; what is a fix or a feature for everyone (`path_is_absolute`,
  `PCSX_MEMCARD_COUNT` with its default of 9, the soft filter on every platform, the 4:3 layer rule, player
  2's analogs, `pl_scanlines_by_plat`) stays unconditional - those are the PR candidates. **A new hook goes
  under the ifdef unless it is meant for everyone.** Upstream's own build knows us: `./configure
  --platform=psclassic` (SDL2 through `sdl2-config`/`SDL2_CONFIG`, `-DPSCLASSIC -DPCSX_MEMCARD_COUNT=2`) and
  the `Makefile`'s `psclassic` block (our platform and `frontend/ab/` objects; the soft filter's objects are
  in the common plugin_lib block) build the same emulator as our CMake, which defines `PSCLASSIC` itself.
  Verified: `--platform=generic` links upstream's `pcsx` with nothing of ours but the soft filter, and
  `--platform=psclassic` with the console toolchain links our emulator (same libraries as the CMake one).
  The libpicofe fork needs no gating: six new files, and of notaz's own only `menu.c`/`menu.h` touched -
  one line each, `menu_sel_name` (the highlighted row's name, which the debug driver reads) - and
  `input.c`/`input.h`: `in_set_menu_quit_check()`, a hook that does nothing until a platform sets it (EMU-18). The CMake build stays the
  one the scripts and CI use; the Makefile path is the shape a future PR to notaz would take.
- **Video**: one SDL2 platform everywhere - window + `SDL_GL_CreateContext` (Wayland on the console, KMSDRM
  on the Pi, WGL on Windows) into libpicofe's `gl.c`, SDL_Renderer as the fallback. No hand-written Wayland
  code (Sony's `gl_platform.c` is in the reference patch if the PowerVR ever needs it back).
- **Engines per target** (CMake pins them): PSC and Pi 32-bit = Ari64 ARM + gpu_neon (NEON asm); Pi 64-bit =
  Ari64 ARM64 + gpu_neon (C SIMD); Windows and Linux = lightrec + gpu_neon (C SIMD). peops/unai stay as options.
- **The contract with AutoBleem does not change**: the run directory (`.pcsx`, `bios`, `plugins` links), the
  command line (`-filter -ratio -lang -region 4 -enter 1 [-load 1] -cdfile`), `pcsx.cfg`'s keys
  (`Bios = SET_BY_PCSX`, `SlowBoot`, the launcher's nine values), `memcards/card1.mcd` (+ `none`),
  and on exit `filename.txt` / `sstates/<name>.000` / `screenshots/<name>.png` / `lastcdimg.txt`,
  and a saved config is `pcsx.custom.cfg` (was `autobleem.cfg` until 2026-09-24). The binary keeps the name `pcsx-ab` inside AutoBleem's payloads so no launcher script
  changes; About and `-v` say pcsx-abnxt.
- **Style**: upstream files keep notaz's style (tabs); ours under `frontend/ab/` follow it - no reformatting
  of upstream code, ever (merge noise). `.gitattributes` covers only our paths (33 upstream files are CRLF and
  stay so).

## Lessons carried over from pcsx-ab (still true here)

- **GCC 14 (the Pi toolchains) vs the console's GCC 6**: `-fcommon` for tentative definitions in headers and
  `.comm` in `linkage_arm.S`; keep a few `-Wno-error=` for pre-existing implicit prototypes. The console's
  binary must need nothing above **GLIBC 2.24** and no RPATH (`tools/check_psc_binary.sh` from
  autobleem-develop gates it).
- **Wayland via SDL2**: `SDL_SysWMinfo::version` must be set before `SDL_GetWindowWMInfo` - unset it read
  uninitialised stack and pcsx-ab segfaulted at its first frame on the console (pcsx-ab2 `70c5dcb`).
- **A window has a surface or a renderer, never both** (SDL >= 2.28 refuses).
- **`SDL_CONTROLLER_BUTTON_MAX` grew** (15 -> 21): an unmapped default of `0` in the GameController key map
  cleared d-pad UP on every new button; the default is `-1`.
- **`SysLibError()` must return NULL on success** - `plugins.c`'s `CheckErr` treats any non-NULL as failure.
- **The console's power/temperature files do not exist elsewhere** - the watcher threads must end at once
  when `access()` fails (pcsx-ab's `fclose(NULL)` segfault on the Pi).
- **`spu.c`'s `tanh()` without `<math.h>`** read its result from r0 on hard-float ARM.
- **CRLF**: pcsx-ab's sources are CRLF; upstream's are LF. Compare with `-w` / strip `\r` before diffing.
  This clone runs with `git config core.autocrlf false` (set once per clone): upstream's files are LF and
  must stay LF in the working copy, or every patch and every upstream merge fights the line endings.
- **`CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`**: a toolchain file's own `-D` variables are invisible inside
  `try_compile` unless listed there.
- The PSC sysroot's `SDL_config.h` defines `SDL_VIDEO_DRIVER_X11` with no X11 headers; the console FindSDL2
  copies the headers with that define removed.

## Working agreements (as in autobleem-develop)

One feature branch per step, one commit per logical change, each building on every target it touches and
smoke-run where a game can prove it (Windows always; the Pi 400 and the console for their phases - the owner
at the console). Commit messages explain why, including what was deliberately not done. Vendored and upstream
code is not made warning-free. Update this file in the same commit when the state table, a decision or the
layout changes. Test material: `D:\AB\Games` (cue/bin, PBP, CHDs with CDDA), `D:\AB\Games (copy)\MDK (US)`.
