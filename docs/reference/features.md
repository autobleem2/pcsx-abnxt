# What pcsx-ab added to PCSX-ReARMed - the inventory

The two patches next to this file are the whole Sony + AutoBleem delta, whitespace-normalised (the console
SDK export is CRLF with re-indented functions; `diff -w` hides that):

- `pcsx-ab-delta-2017.patch` - pcsx-ab (`github.com/autobleem2/pcsx-ab2`, `develop` at `fc8c992`,
  2026-09-20) against its upstream base, `notaz/pcsx_rearmed` **`bebe989b` (2017-10-17)** - r22 + 25 commits,
  the snapshot Sony's 2018 firmware took. 62 files, ~6300 diff lines. Made with
  `diff -ruw -N <upstream bebe989b: libpcsxcore frontend plugins include> <pcsx-ab, CRLF stripped>`;
  `frontend/libpicofe` is excluded (it has its own patch) and so is what pcsx-ab has and upstream never had
  (`CMakeLists.txt`, `toolchains/`, `ci/`, `third_party/libchdr`, `frontend/win32/`, `include/win32_compat.h`).
- `libpicofe-delta-2015.patch` - pcsx-ab's `frontend/libpicofe` against `notaz/libpicofe` **`21604a0`
  (2015-11-08)**. 14 files, ~1850 lines, plus five files only pcsx-ab has (`in_sdl2gc.[ch]`, `keysym.h`,
  `vector.[ch]`).

The line numbers below point into those patches (`sed -n '5132,5160p' pcsx-ab-delta-2017.patch`). The
decisions are in the CLAUDE.md (the plan itself is in the git history); this file only says what is there.

## Console integration (Sony)

| Feature | Where | Patch lines |
|---|---|---|
| The three front buttons as emu actions: `SACTION_CD_CHANGE` (Open, refused for `open_invalid_time` after boot), `SACTION_POWER_OFF`, `SACTION_RESET_EVENT` (save the resume state + screenshot + `filename.txt`, exit) | `frontend/main.c` `do_emu_action` | 5132-5210 |
| The `EJECT`/`AUDIOPLAY` keys bound to them; `bind eject`/`bind reset` names | `frontend/plat_sdl.c` keymap | 6983 |
| Power daemon watch: inotify on `/data/power/prepare_suspend` -> `power_off_flg`; `check_prepare_suspend` at start | `frontend/main.c` | 5736-5800, 5330 |
| CPU temperature watch: `/dev/shm/power/cpu_temp` vs `temp_limit` -> `is_high_temperature` -> `ERROR_CPUOVERHEAT` + reset action | `frontend/main.c`, `plugin_lib.c` | 5541-5640, 7820 |
| `power_off_flg` polled in the emu loop and every menu wait | `plugin_lib.c`, `menu.c` | 7815, 6590-6650 |
| Command line: `-display -lang -region -filter -ratio -enter`, `-v`; `plat_init(isGame, enter_mode)`; Sony's localised PNG names from `lang_list` | `frontend/main.c` main() | 5290-5400 |
| `.pcsx/screenshots/` (`SCSHOT_DIR`), `check_memcards` 1..2, card 2 = `none` | `frontend/main.c` | 5306, 4920 |
| `save_error` -> `/…/NNN.sts` status files (a detached thread; nothing of ours reads them) | `libpcsxcore/misc.c` | 12148-12240 |
| `setCdromId` from the **file name** (not the exe name), `CdromId_old`/`CdromLabel_old`/`CdromPath` | `libpcsxcore/misc.c` | 12300-12340 |
| `setDiscChangeType` (counts `.cue` files in the game folder: 0 = single disc, 1 = swappable) | `libpcsxcore/misc.c` | 12341-12400 |
| `StartCheckOpen`/`CheckOpenEnabled` (the boot grace for the Open button) | `libpcsxcore/misc.c` | 12401 |
| Fast boot with `SlowBoot` (ours, 2026 - upstream has the same) | `libpcsxcore/misc.c` | 12420-12470 |

## The autosave ring (Sony)

| Feature | Where | Patch lines |
|---|---|---|
| `OPT_AUTOSAVE`; every 2 s (`AUTO_SAVE_TO_HEAP_SECOND`) a state into memory, 6 kept (`AUTO_SAVE_TO_FILE_COUNT`), 10 s blocking at boot; skipped while a memory card is written (`memcardFlag`), a reset during a write held (`memcardResetFlag`) | `libpcsxcore/misc.c` `emu_sync_state`, `emu_sync_state2` | 12003-12060, 12882-12990 |
| `SaveStateWork`: the oldest snapshot -> the state file; `getSaveStateArray`, `freeSaveStateMem`, `InitAutoSave` | `libpcsxcore/misc.c` | 12540-12700 |
| The `Mode == 2` (serialise to a buffer / report the size) branches in every freeze: `sio`, `psxcounters`, `mdec`, `dfsound/freeze.c`, `gpulib`, `new_dyna_freeze` (+ `new_dyna_freeze_data`) | `sio.c`, `psxcounters.c`, `mdec.c`, `dfsound/freeze.c`, `gpulib/gpu.c`, `new_dynarec/emu_if.c` | 13640, 13419, 11730, 15499, 17563 (search `Mode == 2`), 13098 |
| `memcardFlag` set on a card write | `libpcsxcore/sio.c` | 13605 |
| `SACTION_SYNC_STATE` from the frame callback | `plugin_lib.c`, `main.c` | 7813, 5125 |

## Disc change (Sony)

| Feature | Where | Patch lines |
|---|---|---|
| `CheckDiscChange` - five state machines over the CD commands (`disc_change_type` 1-5; only 0 and 1 are ever set, the rest are `#if 0`) deciding when a swap is allowed | `libpcsxcore/cdrom.c` | 10301-10420 |
| `swap_cd()`: "single disc" / picker / "not now" screens, `swap_cd_image`, the wait loops | `frontend/menu.c` | 6590-6700 |
| The screens themselves: `show_text_image`, the disc picker drawing Sony's PNGs (`Nomal_Disk.png`, `Corrent_Disk.png`, `Select_Disk.png`, `Text_one..four.png`, `Ball_Btn_SD.png`, the `msg_*_<lang>.png` texts, `OK_SD_Btn_<lang>.png`) from `/usr/sony/share/data/images/` | libpicofe `menu.c` | libpicofe patch 2535-2720 |
| Disc-change state saved with the state (`disc_change_state` appended to the file) | `libpcsxcore/misc.c` SaveState | 12521 |
| `filename.txt` (`make_file_name`: `<label>-<id>` base name, the autosave's `_old` ids when the ring is on) | `frontend/menu.c` | 6843-6900 |
| The EU licence string rewrite (`Sony Computer Entertainment(Europe)` -> `(Eur  ope)` in the sector at 00:02:04) and `SCEI`/blank instead of `PCSX` in `CdlID` | `libpcsxcore/cdrom.c` | 10587-10620, 10486 |

## Per-title hacks (Sony) - not ported, listed so nothing is lost

`libpcsxcore/title.h` (patch 13765-13930): `enum TITLE_NAME` (131 entries) + `stTitleList[]` serial -> title;
`isTitleName()` / `setTitleName()` in `misc.c` (12250-12300), which also raises the GPU patch flags
(`GPU_PATCH_SCREEN_ADJUST`, `IS_FF7`, `IS_MR_DRILLER[_JP]`, `ADD_VRAM2`, `ADD_BO`, `ARC_THE_LAD` ->
`GPUsetPatchFlag`, `gpulib/gpu.h` 17540).

| Hack | Where | Patch lines |
|---|---|---|
| CD: per-sector-range read timing (IQ, Wild Arms, Crash, Arc the Lad, Armored Core, Cool Boarders 2, Destruction Derby, PaRappa, Pacapaca Passion's sector buffer...), per-title `AddIrqQueue` delays, XA volume save/restore around pause, SPU channel kicks, `iTempo`/`RvbConfig` per scene | `libpcsxcore/cdrom.c` | 10508 (the XA shift list), 10620-11450 (`cdrInterrupt` per title) |
| CD: MGS motion-JPEG detection (`check_Motionjpeg`, `check_scenes`) driving `HSyncTotal` 297 and `frame_interval` 18821 | `cdrom.c`, `psxcounters.c`, `plugin_lib.c` | 10397, 13383, 7570 |
| CD: Toshinden CDDA `H_CDLeft`; XA mixing shift 8 for a title list; MediEvil mono mix | `libpcsxcore/cdrom.c` | 10437, 10508-10560 |
| GPU DMA delays: Toshinden (+240000/+340000 cycles), Kagero (+180000) | `libpcsxcore/psxdma.c` | 13454-13500 |
| MDEC: Ridge Racer Type 4 (`words * 3.5`) | `libpcsxcore/mdec.c` | 11698 |
| GPU: second VRAM (`vram2`, FF7 / Mr Driller), `update_sw/sh` clamps, screen-adjust, the "bo"/"pmt" frame-skip command tables (`skip_frame_data[123][12]`, `PMT_CMD`), `GPUboStatus` | `plugins/gpulib/gpu.c`, `dfxvideo/gpulib_if.c` | 17563-18300, 16269-16700 |
| P.E.Op.S.: `isToShinDen`, `scenes`, `regions`, `isBiosLogoEnd`, `iTrimJaggyFrame`, dither modes 2/3, `CHKMAX` 1023/511, 24-bit `NextRow_G4` | `dfxvideo/prim.c`, `soft.c` | 16577-17560 |
| SPU: Suikoden, MGS, `detect_pi`, `SPUenableRvbConfig`, `SPUfadein` (a `tanh` ramp on resume - the one worth keeping), `SPUResetStream`, per-title volume caps and XA per-title lists, `SetDisableVolumeChange` | `dfsound/spu.c`, `registers.c`, `xa.c` | 15887-16148, 15582-15886, 16149-16268; `SPUfadein` 7475 |
| Frame blanking: IQ (JP) last line, Super Puzzle Fighter's bottom 16 lines | `frontend/plugin_lib.c` | 7631-7720 |
| `set_bo_trg` (MGS lineskip trigger) | `frontend/plugin_lib.c` | 7586 |

## Video and input platform (Sony, then AutoBleem)

| Feature | Where | Patch lines |
|---|---|---|
| SDL2 port of the platform: `SDL_MAJOR_VERSION == 2` branches, two keymap sets picked by `-enter`, `plat_gvideo_flip(rgb888)`, `plat_video_menu_enter(is_rom_loaded, bpp)`, `last_shadow_fb`, 640x578x3 shadow | `frontend/plat_sdl.c` | 6938-7470 |
| libpicofe SDL2: `plat_sdl.c` (window/renderer, GL handoff), `in_sdl.c` (`SDLK_to_MY_SDLK` table, `keysym.h`), `sndout_sdl.c` | libpicofe patch | 2839-3780, 608-1510 |
| Wayland + EGL platform (`wl_shell`, `wl_egl_window`) replacing X11 | libpicofe `gl_platform.c` | 239-600 |
| Filter: `GL_LINEAR`/`GL_NEAREST` from `filter_mode`; overlay texture | libpicofe `gl.c` | 9-235 |
| SDL2 GameController driver (ours): two pads, PS button = menu, Select+Start = menu, `-1` unmapped default | libpicofe `in_sdl2gc.c` | 1514-2530 |
| Two pads in the frontend: `in_adev[4]`, `in2_a1/in2_a2`, `in_adev_is_nublike[4]` | `frontend/plugin_lib.c/.h` | 7556, 7861 |
| Port 2 answers "no device" unless `p2_connected` | `libpcsxcore/sio.c` | 13615 |
| `fsync` after every memory-card write | `libpcsxcore/sio.c` | 13664-13760 |
| 24-bit menu background (`bgr888_to_rgb565` in place), full-screen menu bg | `frontend/menu.c` | 6810-6840 |
| Windows layer (ours): `path_is_absolute`, `win32_compat.h`, `SysLibError` NULL | `frontend/main.c` | 5023, 5727 |

## Menu and config (AutoBleem)

| Feature | Where | Patch lines |
|---|---|---|
| Our main menu `e_menu_main3` (Port 1/2 device, Quick Save/Load = slot 2, Toggle Filter, Change CD image, PCSX Menu -> `main_menu1_handler`, Save AutoBleem CFG, Exit); `from_escape` | `frontend/menu.c` | 6692-6790 |
| `autobleem.cfg` = `menu_write_config(2)`, `MA_OPT_SAVECFG_AB` on both option pages | `frontend/menu.c` | 6390 (`menu_write_config` case 2), 6479, 6560 |
| `Bios = SET_BY_PCSX` -> `romJP.bin` (`SLP*`/`SCP*`) else `romw.bin` | `frontend/menu.c` `menu_load_config` | 6390-6410 |
| `config_change()` after load: region -> `PsxAuto/PsxType`, interpolation, `iUseThread = 0`, `gpu_peops.so`, dither, `iTrimJaggyFrame`, `OPT_AUTOSAVE` on, `open_invalid_time` | `frontend/menu.c` | 6107-6180 |
| `CE_CONFIG_VAL(SlowBoot)`, `CE_INTVAL(open_invalid_time)` | `frontend/menu.c` | 6303 |
| "Gamepads are autoconfigured with SDL2 GC API" (the controller pages removed), About text | `frontend/menu.c` | 6469, 6513 |
| CHD (ours; upstream has it): `handlechd`, `cdread_chd`, `uncompressPSC` | `libpcsxcore/cdriso.c` | 9882-10220 |
