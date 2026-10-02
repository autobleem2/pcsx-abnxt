/*
 * What a run leaves behind for AutoBleem's launcher - see ab_session.h.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../libpcsxcore/psxcommon.h"
#include "../../libpcsxcore/misc.h"
#include "../../libpcsxcore/plugins.h"
#include "../libpicofe/readpng.h"
#include "../plugin_lib.h"
#include "../main.h"
#include "ab_config.h"
#include "ab_session.h"

static char game_name[64];
static int exit_saved;

const char *ab_session_game_name(void)
{
	char trimlabel[33];
	int j;

	game_name[0] = 0;
	if (CdromId[0] == 0)
		return game_name;
	/* as main.c's get_gameid_filename(): the label without its trailing spaces, then the id */
	strncpy(trimlabel, CdromLabel, 32);
	trimlabel[32] = 0;
	for (j = 31; j >= 0; j--)
		if (trimlabel[j] == ' ')
			trimlabel[j] = 0;
		else
			break;
	snprintf(game_name, sizeof(game_name), "%.32s-%.9s", trimlabel, CdromId);
	return game_name;
}

static int write_lines(const char *fname, const char *line1, const char *line2)
{
	FILE *f = fopen(fname, "w");
	if (f == NULL) {
		SysPrintf("autobleem: cannot write %s\n", fname);
		return -1;
	}
	fprintf(f, "%s\n", line1);
	if (line2 != NULL)
		fprintf(f, "%s\n", line2);
	fflush(f);
	fsync(fileno(f));
	fclose(f);
	return 0;
}

static int save_live_picture(const char *picture_path)
{
	void *scrbuf;
	int w, h, bpp, ret;

	scrbuf = pl_prepare_screenshot(&w, &h, &bpp);
	if (scrbuf == NULL || bpp != 16) {
		SysPrintf("autobleem: no picture for the resume point (bpp %d)\n", bpp);
		return -1;
	}
	ret = writepng(picture_path, scrbuf, w, h);
	if (ret != 0)
		SysPrintf("autobleem: writepng %s: %d\n", picture_path, ret);
	return ret;
}

int ab_session_save_exit(void)
{
	char state_path[MAXPATHLEN + 64], picture_path[MAXPATHLEN + 64], path[MAXPATHLEN + 64], fname[80];
	const char *iso = GetIsoFile();
	const char *name = ab_session_game_name();
	int ret = 0;

	if (name[0] == 0 || iso == NULL || iso[0] == 0) {
		SysPrintf("autobleem: no disc loaded, nothing to leave behind\n");
		return -1;
	}

	/* the state goes where get_state_filename() puts slot 0, by the disc in the drive: the launcher
	 * records the name (filename.txt) with the disc (lastcdimg.txt) and starts the next run on that
	 * disc, so the two agree again. With $AB_EXIT_DIR all four go there instead, in the same layout -
	 * RAM, from which the launcher keeps them only when the player picks a slot */
	const char *exit_dir = ab_exit_dir();
	if (exit_dir != NULL) {
		mkdir(exit_dir, 0755);
		snprintf(path, sizeof(path), "%s/sstates", exit_dir);
		mkdir(path, 0755);
		snprintf(path, sizeof(path), "%s/screenshots", exit_dir);
		mkdir(path, 0755);
		snprintf(state_path, sizeof(state_path), "%s/sstates/%s.000", exit_dir, name);
		snprintf(picture_path, sizeof(picture_path), "%s/screenshots/%s.png", exit_dir, name);
	} else {
		snprintf(fname, sizeof(fname), "%s.000", name);
		emu_make_path(state_path, sizeof(state_path), STATES_DIR, fname);
		snprintf(fname, sizeof(fname), "%s.png", name);
		emu_make_path(picture_path, sizeof(picture_path), SCREENSHOTS_DIR, fname);
	}

	if (SaveState(state_path) != 0) {
		SysPrintf("autobleem: failed to save %s\n", state_path);
		ret = -1;
	}
	if (save_live_picture(picture_path) != 0)
		ret = -1;

	if (exit_dir != NULL)
		snprintf(path, sizeof(path), "%s/lastcdimg.txt", exit_dir);
	else
		emu_make_path(path, sizeof(path), PCSX_DOT_DIR, "lastcdimg.txt");
	if (write_lines(path, iso, NULL) != 0)
		ret = -1;
	/* last: the launcher takes its presence as "the run ended cleanly" */
	if (exit_dir != NULL)
		snprintf(path, sizeof(path), "%s/filename.txt", exit_dir);
	else
		emu_make_path(path, sizeof(path), PCSX_DOT_DIR, "filename.txt");
	if (write_lines(path, iso, name) != 0)
		ret = -1;

	SysPrintf("autobleem: resume point %s %s\n", name, ret == 0 ? "saved" : "incomplete");
	return ret;
}

void ab_session_exit(void)
{
	if (exit_saved)
		return;
	exit_saved = 1;
	/* every way out ends here (Reset, power-off, overheat, the menu's Exit, SIGTERM, the window's close):
	 * "Please wait..." while the resume point is written, when there is one to write */
	if (ready_to_go && ab_session_game_name()[0] != 0)
		ab_menu_wait_screen();
	ab_session_save_exit();
}
