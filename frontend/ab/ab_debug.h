/*
 * The debug driver: a TCP line server that drives the emulator the way a keyboard would and hands back
 * what is on the screen, so a test can walk the menus without anyone touching the machine - the shape of
 * AutoBleem's own DebugDriver (lib_ableem/include/ableem/ui/debug_driver.h), and tools/ab_drive.py here
 * is the client. Started only when AB_DEBUG_PORT is in the environment; nothing of it runs otherwise.
 *
 * Keys are pushed into SDL's event queue, so they take the very path a real key takes (in_sdl2 -> the
 * binds -> the menu or the game): what the driver tests is what a person would get. The thread only
 * touches the SDL event queue and the frame cache (libpicofe's plat_ab_shot_*), never the emulator's
 * state - anything else would run between CPU slices, which is the emulator's own rule.
 *
 * One command per line, one reply per command ("ok ..." or "err ..."):
 *   press <key> [ms]     down, a hold of ms (60), up - key names are in_sdl2's, lower case: "escape",
 *                        "return", "up", "f9", "z", "eject" (the console's Open button), "reset"
 *   down <key>/up <key>  a held key (the menu button's 2 s hold = Reset)
 *   wait <ms>            sleep
 *   frames               how many frames have been presented
 *   shot <file.bmp>      the next frame presented, written out (the emulator's own screenshot is the raw
 *                        PSX frame; this is the screen, filters, scanlines and all). A menu draws only
 *                        when something happens, so the shot asks the window to redraw itself first
 *   screen               what is showing: boot, game, menu, pcsx, disc, message
 *   row                  the name of the menu row the cursor is on (libpicofe's menu_sel_name), so a
 *                        script walks the menus by name instead of counting keypresses
 *   status               game=<id> screen=<name> row=<name> frames=<n> ready=<0|1> ...
 *   quit                 an SDL_QUIT, as the window's close button
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PCSXAB_AB_DEBUG_H
#define PCSXAB_AB_DEBUG_H

/* AB_DEBUG_PORT=<port> in the environment: listen on 127.0.0.1:<port> from a thread of its own for the
 * rest of the run. Called once, from plat_init(); does nothing without the variable */
void ab_debug_start(void);

/* what is on the screen now, for the driver's `screen` - our menu code says so as it draws (a cheap
 * pointer store; it costs nothing when no driver runs) */
void ab_debug_screen(const char *name);

#endif /* PCSXAB_AB_DEBUG_H */
