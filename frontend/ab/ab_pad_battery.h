/*
 * A wireless pad's battery, from the kernel's power_supply sysfs tree - the C port of AutoBleem's
 * PadBatteryService (autobleem-core, src/code/core/services/pad_battery.h, C8/E15): same two folder-name
 * prefixes, same capacity_level -> percent mapping, same low/reset thresholds. Kept here rather than shared
 * with core because this emulator does not link ab_core (it is not SDL/C++, and ships to targets ab_core
 * never builds for).
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PCSXAB_AB_PAD_BATTERY_H
#define PCSXAB_AB_PAD_BATTERY_H

/* a pad at or under this percent is "low" */
#define AB_PAD_BATTERY_LOW_PERCENT 15
/* ...and has to climb back over this before it counts as low again - keeps a reading bouncing around 15%
 * from flickering the icon on and off every poll (mirrors PadBatteryPollInterval's hysteresis pair) */
#define AB_PAD_BATTERY_RESET_PERCENT 25
/* how often the sysfs tree is actually read, milliseconds - a handful of small file reads, cheap enough to
 * poll but not worth doing every frame (PadBatteryPollInterval in core/model/timing.h) */
#define AB_PAD_BATTERY_POLL_MS (5 * 1000)

/* call every frame (from ab_frame_tick); the sysfs reads themselves only happen once every
 * AB_PAD_BATTERY_POLL_MS - cheap to call more often than that */
void ab_pad_battery_tick(unsigned int now_ms);

/* 1 once a poll found a wireless pad at or under AB_PAD_BATTERY_LOW_PERCENT, until one climbs back over
 * AB_PAD_BATTERY_RESET_PERCENT (or every pad's battery node goes away - unplugged, or wired) */
int ab_pad_battery_low(void);

/* 1 when the corner icon should be drawn this frame: only while a pad is low (the in-game menu shows every
 * pad's battery; the show-while-the-menu-button-is-held is gone - that button opens the menu and, held,
 * leaves the game) */
int ab_pad_battery_visible(void);

/* the power_supply entry's `status`: a pad that is "Full" is never low (the icon goes at the next poll, whatever
 * its capacity says); a low pad that is "Charging" keeps the icon, and this says so - 1 while the pad the icon
 * is about (the lowest) is Charging, so the HUD draws a bolt over the fill; it clears at the reset threshold as
 * before. "Discharging", "Not charging", "Unknown" or no file: as before. */
int ab_pad_battery_charging(void);

/* every wireless pad's percent at the last poll (the in-game menu's header), in a stable order; returns
 * how many, 0 when no pad's battery is known (wired pads have none) */
#define AB_PAD_BATTERY_MAX 4
int ab_pad_battery_all(int *percent, int max);

/* the percent the icon should fill to: whichever reading made ab_pad_battery_low() true, or the lowest
 * known percent; -1 when nothing is known */
int ab_pad_battery_percent(void);

#endif
