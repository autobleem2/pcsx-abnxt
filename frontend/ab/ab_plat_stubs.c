/*
 * What our SDL2 platform (plat_autobleem.c, ab_debug.c) gives the shared AutoBleem code, for a platform
 * without it - today upstream's SDL 1.2 frontend (-DPCSXAB_PLATFORM=sdl, a PC with sdl12-compat; BUG-19):
 * no debug driver (it pushes SDL2 events into that platform's window and reads its frames), and one output
 * mode, the window's own, so the menu's Display row stays greyed.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>

#include "ab_config.h"
#include "ab_debug.h"

void ab_debug_start(void)
{
}

void ab_debug_screen(const char *name)
{
	(void)name;
}

int ab_output_mode = AB_OUTPUT_AUTO;

int ab_output_modes(int *modes, int max)
{
	if (max < 1)
		return 0;
	modes[0] = AB_OUTPUT_AUTO;
	return 1;
}

void ab_output_mode_name(int mode, char *buf, int size)
{
	(void)mode;
	snprintf(buf, size, "Auto");
}

int ab_output_mode_apply(int mode, int tell_launcher)
{
	(void)tell_launcher;
	return mode == AB_OUTPUT_AUTO ? 0 : -1;
}
