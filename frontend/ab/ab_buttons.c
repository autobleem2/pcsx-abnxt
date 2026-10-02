/*
 * The front buttons and the per-frame tick - see ab_buttons.h.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <string.h>

#include "../../libpcsxcore/psxcommon.h"
#include "../../libpcsxcore/r3000a.h"
#include "../../libpcsxcore/misc.h"
#include "../../libpcsxcore/state_sony.h"
#include "../libpicofe/plat.h"
#include "../main.h"
#include "ab_buttons.h"
#include "ab_memcard.h"
#include "ab_console.h"
#include "ab_debug.h"
#include "ab_disc.h"
#include "ab_pad_battery.h"

extern enum sched_action emu_action, emu_action_old;

static int exit_held;		/* a way out waiting (the memory card, a clean frame): its action */
static int exit_frames;		/* frames still to present before it goes */
static int exit_clean;		/* those frames were presented without the HUD */
static int power_off_seen, overheat_seen;
static unsigned int autosave_last;	/* the tick of the last autosave (or of the game's start) */

#define AB_MENU_HOLD_MS   2000	/* the menu button held this long is Reset */
#define AB_MENU_HINT_MS   500	/* ...and says so on the HUD from here */
#define AB_AUTOSAVE_MS    30000	/* how often the game is autosaved (RAM only) */
static const char hold_hint[] = "HOLD TO EXIT";

void ab_request_action(int action)
{
	/* plugin_lib's emu_set_action(), which is static there */
	if (action == SACTION_NONE)
		emu_action_old = 0;
	else if (action != emu_action_old)
		psxRegs.stop++;
	emu_action = action;
}

/* The run ends: main()'s loop stops at the end of this CPU slice, and ab_session_exit() saves the game as
 * it is at that moment as the resume point. Not while the game is writing its memory card - the state and
 * the card file would disagree - so then the way out waits for the write to be over (ab_frame_tick). And
 * the resume point's picture is the frame on screen, into which the HUD is printed ("HOLD TO EXIT",
 * "SAVING..."): the HUD goes first and two more frames are presented without it. */
static void leave(int action, const char *why)
{
	if (ab_memcard_busy()) {
		if (!exit_held) {
			SysPrintf("autobleem: %s held, the memory card is being written\n", why);
			snprintf(hud_msg, sizeof(hud_msg), "SAVING...");
			hud_new_msg = 3;
		}
		exit_held = action;
		exit_clean = 0;
		return;
	}
	if (!exit_clean) {
		hud_msg[0] = 0;
		exit_held = action;
		exit_frames = 2;
		exit_clean = 1;
		return;
	}
	SysPrintf("autobleem: %s - leaving with the resume point\n", why);
	emu_core_ask_exit();
}

void ab_defer_action(int action)
{
	/* exit_held is what ab_frame_tick() hands to ab_request_action(); with no frames to wait it does so at once */
	if (!exit_held)
		exit_held = action;
}

int ab_filter_action(int action)
{
	static int held, fired;
	static unsigned int held_since;
	unsigned int now;

	/* the console too (2026-09-24; the menu used to open at once there): its pad has no Home, and
	 * Select+Start held is the way out that does not mean reaching for the front of the console */
	now = plat_get_ticks_ms();
	if (action == SACTION_ENTER_MENU) {
		if (!held) {
			held = 1;
			fired = 0;
			held_since = now;
		} else if (!fired && now - held_since >= AB_MENU_HOLD_MS) {
			fired = 1;
			hud_msg[0] = 0;
			return SACTION_AB_RESET;
		} else if (!fired && now - held_since >= AB_MENU_HINT_MS && hud_msg[0] == 0) {
			snprintf(hud_msg, sizeof(hud_msg), "%s", hold_hint);
			hud_new_msg = 2;
		}
		return SACTION_NONE;
	}
	if (held) {
		/* released: a press, unless the hold already went out as Reset */
		held = 0;
		if (strcmp(hud_msg, hold_hint) == 0)
			hud_msg[0] = 0;
		if (!fired)
			return SACTION_ENTER_MENU;
	}
	return action;
}

int ab_emu_action(int action)
{
	switch (action) {
	case SACTION_AB_RESET:
		leave(action, "Reset");
		return 1;
	case SACTION_AB_POWER_OFF:
		leave(action, "Power");
		return 1;
	case SACTION_AB_CD_CHANGE:
		ab_disc_change();
		return 1;
	case SACTION_AB_AUTOSAVE:
		/* between two CPU slices, as every state save; not while the game writes its card */
		autosave_last = plat_get_ticks_ms();
		if (!ab_memcard_busy())
			SaveStateAuto(autosave_last);
		return 1;
	default:
		return 0;
	}
}

void ab_frame_tick(void)
{
	static int started;

	ab_debug_screen("game");	/* a frame of the game: what the debug driver reports until a menu draws */
	ab_pad_battery_tick(plat_get_ticks_ms());	/* throttled to AB_PAD_BATTERY_POLL_MS internally */
	if (!started) {
		started = 1;
		ab_console_start();
		autosave_last = plat_get_ticks_ms();
	}

	if (ab_console_power_off_requested && !power_off_seen) {
		power_off_seen = 1;
		ab_request_action(SACTION_AB_POWER_OFF);
		return;
	}
	if (ab_console_overheated && !overheat_seen) {
		overheat_seen = 1;
		ab_request_action(SACTION_AB_RESET);
		return;
	}
	if (exit_held && !ab_memcard_busy()) {
		int action = exit_held;
		if (exit_frames > 0) {
			exit_frames--;
			return;
		}
		exit_held = 0;
		ab_request_action(action);
		return;
	}

	/* the autosave, every AB_AUTOSAVE_MS of play, in RAM (the menu's "Load autosave"); only when nothing
	 * else is waiting to run */
	if (ready_to_go && CdromId[0] && emu_action == SACTION_NONE &&
	    plat_get_ticks_ms() - autosave_last >= AB_AUTOSAVE_MS)
		ab_request_action(SACTION_AB_AUTOSAVE);

	ab_disc_tick();
}
