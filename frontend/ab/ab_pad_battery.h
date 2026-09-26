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

/* the menu button is being held (ab_buttons.c's "HOLD TO EXIT" wait) - while true the icon shows on demand
 * even when the pad is not low (as long as some wireless pad's battery is known at all), so a press is a
 * quick way to check it */
void ab_pad_battery_set_show_requested(int show);

/* 1 when the corner icon should be drawn this frame: ab_pad_battery_low(), or a show request with a known
 * percent to draw */
int ab_pad_battery_visible(void);

/* the percent the icon should fill to: whichever reading made ab_pad_battery_low() true, or the lowest
 * known percent for a plain show request; -1 when nothing is known (ab_pad_battery_visible() is then 0) */
int ab_pad_battery_percent(void);

#endif
