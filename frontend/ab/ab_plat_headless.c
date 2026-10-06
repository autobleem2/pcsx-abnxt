/*
 * What the sdl2 platform (plat_autobleem.c, ab_debug.c) gives the rest of frontend/ab/, for the headless
 * platform (-DPCSXAB_PLATFORM=headless, a link check with no video and no input): no debug driver, and no
 * display to pick an output mode on - the menu's Display row offers only "Auto".
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>

#include "ab_config.h"
#include "ab_debug.h"

int ab_output_mode;
int ab_crt_margin = 5;

/* no display, so never a CRT nor a 4:3 output */
int ab_crt43(void)
{
	return 0;
}

int ab_layout43(void)
{
	return 0;
}

void ab_debug_screen(const char *name)
{
	(void)name;
}

int ab_output_modes(int *modes, int max)
{
	if (max < 1)
		return 0;
	modes[0] = AB_OUTPUT_AUTO;
	return 1;
}

void ab_output_mode_name(int mode, char *buf, int size)
{
	if (mode == AB_OUTPUT_AUTO)
		snprintf(buf, size, "Auto");
	else
		snprintf(buf, size, "%dx%d", AB_OUTPUT_W(mode), AB_OUTPUT_H(mode));
}

int ab_output_mode_apply(int mode, int tell_launcher)
{
	(void)tell_launcher;
	ab_output_mode = AB_OUTPUT_AUTO;
	return mode == AB_OUTPUT_AUTO ? 0 : -1;
}
