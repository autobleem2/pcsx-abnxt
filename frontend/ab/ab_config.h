/*
 * AutoBleem's command line and configuration on top of pcsx-rearmed's: what rc/launch.sh passes and what
 * the launcher writes into pcsx.cfg (the contract with AutoBleem - CLAUDE.md).
 *
 *   pcsx-ab -filter F -ratio R -lang L -region N -enter E [-display D] [-load 1] [-language Name] -cdfile <image>
 *
 * and, for a launcher that cannot lay the run directory out with symlinks (the Windows product starts
 * the emulator directly - there is no launch.sh, and no symlinks on a FAT stick):
 *
 *   -dotdir DIR    the profile folder itself - what .pcsx/ in the working directory is otherwise:
 *                  pcsx.cfg, memcards/, sstates/, screenshots/ and the launcher's files live there
 *   -biosdir DIR   the BIOS folder (bios/ in the working directory otherwise)
 *   -fullscreen    the whole display, whatever a pcsx.cfg's vout_fullscreen says (a desktop build
 *                  opens a window by default; the console and the Pi are full screen regardless)
 *   -sonyhacks     Sony's per-title configuration overrides for the disc's serial, over the pcsx.cfg
 *                  (ab_hacks.h; off by default - a lever for the compatibility pass)
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PCSXAB_AB_CONFIG_H
#define PCSXAB_AB_CONFIG_H

/*
 * A game's configuration has one source at a time (2026-09-24):
 *
 *   pcsx.cfg          AutoBleem's - the launcher's game editor writes it, launch.sh puts it in .pcsx/
 *   pcsx.custom.cfg   the game's own, in .pcsx/ (its !SaveStates folder): every save in the emulator's
 *                     menus writes it ("Save settings for this game"), keeping the keys it does not know
 *                     (pcsx-ab's), and the launcher shows the game's settings locked while it exists -
 *                     "Unlock" there deletes it and pcsx.cfg is the game's again
 *
 * At the game's start pcsx.cfg is loaded, then pcsx.custom.cfg over it (a key it lacks keeps AutoBleem's
 * value), and a key it has beats the launcher's -filter/-ratio. pcsx-ab (pcsx-ab2) does the same.
 */
#define AB_CUSTOM_CFG "pcsx.custom.cfg"
/* what the launcher writes into pcsx.cfg's Bios key: the BIOS by the disc's region */
#define AB_BIOS_SET_BY_PCSX "SET_BY_PCSX"

struct ab_options {
	int filter;	/* -filter: 1 = bilinear scaling, 0 = nearest */
	int ratio;	/* -ratio: 1 = fill the 16:9 screen, 0 = keep 4:3 */
	int lang;	/* -lang: the console UI's language, 1..13 (Sony's numbering; 13 = Japanese) */
	int region;	/* -region: accepted for the launch script's sake, the disc decides */
	int enter;	/* -enter: accepted, always 1 from the launcher */
	int display;	/* -display: accepted, unused */
	/* -language: the launcher's language by the name of its lang file (English, Polski, Chinese_Simplified...),
	 * what lang/<Name>.txt next to the emulator translates the emulator's own screens with (ab_ui.h);
	 * only pcsx-abnxt gets it - the launch scripts keep it from the classic pcsx-ab */
	char language[64];
	int fullscreen;	/* -fullscreen: the display is ours, no window */
	int sonyhacks;	/* -sonyhacks: Sony's per-title configuration overrides by serial (ab_hacks.h) */
	const char *dotdir;	/* -dotdir: the profile folder, NULL = <home>/.pcsx as upstream has it */
	const char *biosdir;	/* -biosdir: the BIOS folder, NULL = the profile's bios/ (or ./bios) */
};
extern struct ab_options ab_opts;

/* Takes AutoBleem's options out of argv before main() parses the rest (it treats an unknown argument as
 * an executable to load). Returns the new argc. */
int ab_args_take(int argc, char *argv[]);

/* A profile path with -dotdir honoured: `dir` is one of upstream's PCSX_DOT_DIR-rooted constants
 * ("/.pcsx/memcards/" ...), `fname` may be NULL. Without -dotdir it is what emu_make_path always was:
 * <home><dir><fname>. main.c's emu_make_path calls this. */
void ab_make_path(char *buf, size_t size, const char *home, const char *dir, const char *fname);

/* The same for main.c's get_gameid_filename formats, "%s" PCSX_DOT_DIR "<sub>/<name format>": with
 * -dotdir the format comes back in `out` as "%s/<sub>/<name format>" and the returned home is the dotdir;
 * without it `out` is `fmt` as it was and `home` is returned. */
const char *ab_gameid_format(const char *fmt, char *out, size_t size, const char *home);

/* After menu_load_config(is_game) parsed a config file: "Bios = SET_BY_PCSX" becomes the per-region BIOS
 * files AutoBleem's System/Bios holds (romJP.bin for Japan, romw.bin for the rest - the core then picks by
 * the disc's region, HLE when the file is missing), an unset second memory card is "none" (the launcher
 * swaps card1.mcd in and out and nothing else), and the command line's filter/ratio win over the file -
 * unless the game's own config (is_game) has the key. */
void ab_config_loaded(int is_game);

/* 1 while the BIOS is the one "SET_BY_PCSX" picked: a save writes "SET_BY_PCSX" back, not the file name */
int ab_bios_set_by_pcsx(void);

/* What the launcher hands over through the environment - each one listed in the abfeatures file next to
 * the binary, which is how a launcher knows this build takes it (an older one ignores them); NULL when not
 * given. AutoBleem's quiet-stick plan: nothing written to the stick that did not have to be.
 *   AB_EXIT_DIR     where the resume point of the way out goes (ab_session.h), in RAM - the launcher copies
 *                   it to the stick only when the player keeps it           (abfeatures: exitdir)
 *   AB_MEMCARD_DIR  the memory-card set this game plays with, used in place  (abfeatures: memcarddir)
 *   AB_LOAD_STATE   a state file to start from - a kept resume slot, read where it is instead of copied to
 *                   slot 0 first                                             (abfeatures: loadstate)
 *   AB_PAD_ORDER    C11, Options -> "Swap Player 1 / Player 2": "1,0" swaps which of the first two SDL
 *                   pads (by acceptance order, see plat_autobleem.c's pads_changed()) lands on PS1 port 1 vs 2
 *                   - a purely positional permutation, no pad identity involved (abfeatures: padorder).
 *                   Only takes effect with two or more pads connected - pads_changed() forces the identity
 *                   order itself when its pad_count is under 2, so ab_pad_order() need not know how many
 *                   pads there are; a lone pad is always player 1, swap on or off. */
const char *ab_exit_dir(void);
const char *ab_memcard_dir(void);

/* The output mode (plat_autobleem.c): AB_OUTPUT_MODE from the launcher - "auto" (the display's own mode),
 * "720" or "1080" (abfeatures: outputmode) - and the menu's Display row. A mode the display does not list
 * falls back to auto. A change in the menu is written to $AB_RUNTIME_DIR/outputmode (RAM), one line with
 * the same token, for the launcher to take into its own setting when the game ends. */
enum { AB_OUTPUT_AUTO, AB_OUTPUT_720, AB_OUTPUT_1080 };
extern int ab_output_mode;
int ab_output_mode_parse(const char *s);
int ab_output_mode_available(int mode);
int ab_output_mode_apply(int mode, int tell_launcher);
const char *ab_load_state(void);
/* order[0]/order[1] = the PS1 port (0-based) SDL pad index 0/1 lands on; {0, 1} (AB_PAD_ORDER unset,
 * malformed, or not a permutation of {0, 1}) is the original, unswapped order. Says nothing about how many
 * pads are actually connected - the caller (plat_autobleem.c's pads_changed()) is what applies the "only with
 * two or more pads" rule. */
void ab_pad_order(int order[2]);

#endif
