/*
 * What a run leaves behind for AutoBleem's launcher (ResumePointService reads them from the game's
 * !SaveStates folder, which is .pcsx/ here - or from $AB_EXIT_DIR, in the same layout, when the launcher
 * gave one: RAM, so the stick is written only if the player keeps the resume point):
 *
 *   .pcsx/sstates/<label>-<id>.000       the resume state (save slot 0)
 *   .pcsx/screenshots/<label>-<id>.png   its picture
 *   .pcsx/filename.txt                   line 1 the disc image, line 2 the "<label>-<id>" base name
 *   .pcsx/lastcdimg.txt                  the disc image in the drive when the run ended
 *
 * A run that was killed leaves no filename.txt, which is how the launcher tells a clean exit from a
 * crash. Written on every way out while a disc is loaded - the menu's Exit, the window's close button,
 * the menu button held, the console's Reset and Power buttons (ab_buttons) - from main() once its loop
 * is out, between two CPU slices: the game as it is at that moment. The "<label>-<id>" name is the disc
 * in the drive at the end: the launcher records it with the disc image and starts the resume on that disc.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PCSXAB_AB_SESSION_H
#define PCSXAB_AB_SESSION_H

/* the "<label>-<id>" base name of the run's files, as main.c derives it for the save states; "" without
 * a disc */
const char *ab_session_game_name(void);

/* the resume state, its picture and the two text files; 0 when all of them are written */
int ab_session_save_exit(void);

/* main()'s way out: ab_session_save_exit() once, whichever path led here, with "Please wait..." on the screen */
void ab_session_exit(void);

/* that "Please wait..." screen (ab_menu.c, the menu's look) */
void ab_menu_wait_screen(void);

#endif
