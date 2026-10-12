/*
 * The disc set of the running game and the Open button: which images belong to the game (the discs of a
 * multi-disc PBP, an .m3u next to the image, or the game folder's own images of the same kind) and the
 * change from one to the next through the core's CD lid (SetCdOpenCaseTime/LidInterrupt, as upstream's
 * menu swaps discs).
 *
 * The Open button (and the menu's "Change disc") opens the disc picker over the paused game
 * (ab_menu_change_disc, in ab_menu.c): the set's discs in a row, the one in the drive marked, the next
 * one focused, Left/Right and Cross put a disc in through the lid, Circle backs out. A single-disc game
 * gets a message instead, and so does a press in the first AB_OPEN_GRACE_S seconds of the run (Sony's
 * open_invalid_time: a game does not survive a lid opened while it boots). The screens' text is in the
 * launcher's language (ab_ui.h).
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PCSXAB_AB_DISC_H
#define PCSXAB_AB_DISC_H

#define AB_OPEN_GRACE_S 22
#define AB_DISC_MAX 8

/* the Open button: the picker over the paused game */
void ab_disc_change(void);
/* a change is allowed now (past the first AB_OPEN_GRACE_S seconds of the run) */
int ab_disc_can_change(void);
/* puts disc `index` (0-based) in the drive through the lid, with a HUD line; 0 = done */
int ab_disc_insert(int index);
/* the picker screen, ab_menu.c (menu.c's unit): leaves the emulator, shows it, comes back */
void ab_menu_change_disc(void);
/* once per frame: learns the set when the disc id becomes known */
void ab_disc_tick(void);
/* how many discs the set has (1 = no change possible), and which is in the drive (0-based) */
int ab_disc_count(void);
int ab_disc_current(void);

/* A real disc in a drive (-cdfile cdrom://driveN.cue, PCSXAB_PHYSICAL_CDROM): there is no set to pick from -
 * the drive's tray is the lid. ab_disc_tick() asks the drive about its tray now and then (cdra_check_eject,
 * every ~100 frames, as upstream's libretro core does) and a change asks for SACTION_AB_TRAY; its handler
 * (ab_disc_tray, between CPU slices) opens or closes the core's lid and, on a close, reopens the drive for
 * the disc now in it. The Open button and "Change disc" say to use the tray. */
int ab_disc_in_drive(void);
void ab_disc_tray(void);

#endif
