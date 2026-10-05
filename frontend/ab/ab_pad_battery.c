/*
 * ab_pad_battery: see ab_pad_battery.h. The folder-name rule and the capacity_level mapping are the exact
 * ones PadBatteryService uses (autobleem-core, C8) - keep the two in step if either changes.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "ab_pad_battery.h"

static int low_latched;	/* 1 once a pad was seen at/under LOW_PERCENT, cleared only at/over RESET_PERCENT */
static int last_percent = -1;	/* the reading low_latched is based on, or the lowest seen this poll */
static int last_charging;	/* 1 when the pad last_percent belongs to says "Charging" */
static int poll_lowest_real;	/* that pad's own percent (a Full pad is counted as 100 for the latch only) */
/* every wireless pad's percent at the last poll, in the order of their sysfs names (the menu's header) */
static int all_percent[AB_PAD_BATTERY_MAX];
static char all_name[AB_PAD_BATTERY_MAX][64];
static int all_count;

/* the whole of a small sysfs file's first line, trimmed of the trailing newline/whitespace; "" (and 0
 * returned) when the file is not there - a driver that has not written a value yet, or a kernel without
 * this file at all, just leaves the row unknown, same as PadBatteryService::readFirstLine */
static int read_first_line(const char *path, char *buf, size_t bufsize)
{
	FILE *f = fopen(path, "rb");
	size_t n;
	if (f == NULL)
		return 0;
	n = fread(buf, 1, bufsize - 1, f);
	fclose(f);
	buf[n] = 0;

	/* first line only */
	{
		char *nl = strpbrk(buf, "\r\n");
		if (nl != NULL)
			*nl = 0;
	}
	/* trim trailing spaces (the files never lead with any) */
	{
		size_t len = strlen(buf);
		while (len > 0 && buf[len - 1] == ' ')
			buf[--len] = 0;
	}
	return buf[0] != 0;
}

/* "sony_controller_battery_<mac>" / "ps-controller-battery-<mac>" - PadBatteryService::isPadBatteryEntry */
static int is_pad_battery_entry(const char *name)
{
	static const char *prefixes[] = { "sony_controller_battery_", "ps-controller-battery-" };
	size_t i;
	for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
		size_t plen = strlen(prefixes[i]);
		if (strlen(name) > plen && strncmp(name, prefixes[i], plen) == 0)
			return 1;
	}
	return 0;
}

/* "Full"/"High"/"Normal"/"Low"/"Critical" -> an approximate percent, for a kernel with no plain "capacity"
 * file - PadBatteryService::percentFromCapacityLevel, same numbers */
static int percent_from_capacity_level(const char *level)
{
	if (strcmp(level, "Full") == 0)
		return 100;
	if (strcmp(level, "High") == 0)
		return 75;
	if (strcmp(level, "Normal") == 0)
		return 50;
	if (strcmp(level, "Low") == 0)
		return 15;
	if (strcmp(level, "Critical") == 0)
		return 5;
	return -1; /* "Unknown", or anything this list does not know */
}

/* Env::padBatteryPowerSupplyDir(): $AB_PAD_BATTERY_DIR overrides on every platform (a fake tree for
 * testing, on a dev host with none of its own); the real sysfs tree otherwise, "" (poll finds nothing) on
 * a target that has none */
static const char *power_supply_dir(void)
{
	const char *env = getenv("AB_PAD_BATTERY_DIR");
	if (env != NULL && env[0] != 0)
		return env;
#ifdef _WIN32
	return "";
#else
	return "/sys/class/power_supply";
#endif
}

/* one poll of the sysfs tree: the lowest percent among every wireless pad's battery node found there, or
 * -1 when the root does not exist or nothing under it matches */
static int poll_lowest_percent(void)
{
	const char *root = power_supply_dir();
	DIR *d;
	struct dirent *ent;
	int lowest = -1;

	all_count = 0;
	last_charging = 0;
	poll_lowest_real = -1;
	if (root[0] == 0)
		return -1;
	d = opendir(root);
	if (d == NULL)
		return -1;

	while ((ent = readdir(d)) != NULL) {
		char path[512], buf[64];
		int percent = -1;

		if (!is_pad_battery_entry(ent->d_name))
			continue;

		snprintf(path, sizeof(path), "%s/%s/capacity", root, ent->d_name);
		if (read_first_line(path, buf, sizeof(buf))) {
			char *end = NULL;
			long v = strtol(buf, &end, 10);
			if (end != buf) {
				if (v < 0)
					v = 0;
				if (v > 100)
					v = 100;
				percent = (int)v;
			}
		}
		if (percent < 0) {
			snprintf(path, sizeof(path), "%s/%s/capacity_level", root, ent->d_name);
			if (read_first_line(path, buf, sizeof(buf)))
				percent = percent_from_capacity_level(buf);
		}
		/* "Charging" / "Full" / "Discharging" / "Not charging" / "Unknown": a pad that is Full is never low (its
		 * capacity can lag behind on some drivers), a Charging one is still low but the icon says it charges */
		if (percent >= 0) {
			int charging = 0, effective = percent;
			snprintf(path, sizeof(path), "%s/%s/status", root, ent->d_name);
			if (read_first_line(path, buf, sizeof(buf))) {
				if (strcmp(buf, "Full") == 0)
					effective = 100;
				else if (strcmp(buf, "Charging") == 0)
					charging = 1;
			}
			if (lowest < 0 || effective < lowest) {
				lowest = effective;
				poll_lowest_real = percent;
				last_charging = charging;
			}
		}
		if (percent >= 0 && all_count < AB_PAD_BATTERY_MAX) {
			/* sorted by name, so the order stays put between polls */
			int i = all_count++;
			while (i > 0 && strcmp(all_name[i - 1], ent->d_name) > 0) {
				all_percent[i] = all_percent[i - 1];
				memcpy(all_name[i], all_name[i - 1], sizeof(all_name[i]));
				i--;
			}
			all_percent[i] = percent;
			snprintf(all_name[i], sizeof(all_name[i]), "%s", ent->d_name);
		}
	}
	closedir(d);
	return lowest;
}

void ab_pad_battery_tick(unsigned int now_ms)
{
	static unsigned int last_poll_ms;
	static int polled_once;
	int lowest;

	if (polled_once && now_ms - last_poll_ms < AB_PAD_BATTERY_POLL_MS)
		return;
	polled_once = 1;
	last_poll_ms = now_ms;

	lowest = poll_lowest_percent();	/* a Full pad counts as 100 here */
	last_percent = lowest < 0 ? -1 : poll_lowest_real;

	if (lowest >= 0 && lowest <= AB_PAD_BATTERY_LOW_PERCENT)
		low_latched = 1;
	else if (lowest < 0 || lowest >= AB_PAD_BATTERY_RESET_PERCENT)
		low_latched = 0;
	/* between the two thresholds (or unknown while already latched): keep the last state */
}

int ab_pad_battery_low(void)
{
	return low_latched;
}

int ab_pad_battery_visible(void)
{
	return low_latched;
}

int ab_pad_battery_charging(void)
{
	return last_charging;
}

int ab_pad_battery_percent(void)
{
	return last_percent;
}

int ab_pad_battery_all(int *percent, int max)
{
	int i, n = all_count < max ? all_count : max;

	for (i = 0; i < n; i++)
		percent[i] = all_percent[i];
	return n;
}
