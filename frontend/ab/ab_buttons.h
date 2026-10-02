/*
 * The PlayStation Classic's front buttons as emulator actions, and the per-frame tick that carries the
 * console's own events (the power daemon, the CPU temperature).
 *
 *   Reset  (the AUDIOPLAY scancode, "RESET button" in pcsx.cfg)   -> exit, the game as it is now the
 *                                                                    resume point (ab_session)
 *   Open   (the EJECT scancode, "CD Change button")                -> the disc change (ab_disc)
 *   Power  (/data/power/prepare_suspend appears - ab_console)      -> as Reset
 *   too hot (the power daemon's cpu_temp over its limit)           -> as Reset
 *
 * A way out that lands while the game is writing its memory card waits until the write is over
 * (ab_memcard), so the resume point never contains half a card (Sony's memcardResetFlag). Until
 * 2026-09-24 Reset and Power left an autosave ring's oldest snapshot, ~10 s back, as Sony's firmware
 * did; the owner wanted the moment the player left instead, and the ring went.
 *
 * The menu button (the pad's Home, Select+Start on a pad without one - the console's own - or Escape) does
 * two things: a press opens the menu when it is released, a hold of 2 s is Reset (ab_filter_action). On
 * every platform: off the console there are no front buttons at all, and on it (since 2026-09-24; the menu
 * used to open at once there) leaving a game should not mean reaching for the front of the console.
 *
 * Everything that touches the emulator's state (a disc change, the exit) runs as an emulator action,
 * between two CPU slices; the frame tick only decides and asks (ab_request_action).
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PCSXAB_AB_BUTTONS_H
#define PCSXAB_AB_BUTTONS_H

/* do_emu_action()'s default branch: 1 when the action was one of ours */
int ab_emu_action(int action);

/* once per frame from pl_frame_limit(): the console's flags, a way out waiting for the memory card */
void ab_frame_tick(void);

/* asks the emulator to run <action> (SACTION_*) as soon as the current slice ends, from the main thread */
void ab_request_action(int action);

/* <action> (a way out: SACTION_AB_RESET) asked for while a menu is open: the first frame tick after the menu
 * asks for it, so it runs the in-game way (ab_emu_action -> leave()). Nothing if a way out is pending already */
void ab_defer_action(int action);

/* update_input()'s emulator action, with the menu button's press/hold told apart (see the top); called
 * every frame with SACTION_NONE when nothing is pressed */
int ab_filter_action(int action);

#endif
