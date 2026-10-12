/*
 * The disc set and the Open button - see ab_disc.h.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dirent.h>

#include "../../libpcsxcore/psxcommon.h"
#include "../../libpcsxcore/misc.h"
#include "../../libpcsxcore/plugins.h"
#include "../../libpcsxcore/cdrom.h"
#include "../../libpcsxcore/cdriso.h"
#include "../../libpcsxcore/cdrom-async.h"
#include "../libpicofe/plat.h"
#include "../main.h"
#include "ab_buttons.h"
#include "ab_disc.h"

extern enum sched_action emu_action;

enum disc_kind { DISCS_NONE, DISCS_SINGLE, DISCS_PBP, DISCS_FILES };

static enum disc_kind kind = DISCS_NONE;
static char discs[AB_DISC_MAX][MAXPATHLEN];
static int disc_count, disc_current;
static unsigned int start_ms;
/* a real disc in a drive: the core's lid as the drive's tray last told us, and the tray as it is now,
 * waiting for SACTION_AB_TRAY */
static int tray_open, tray_now, tray_pending;

static void hud(const char *msg)
{
	snprintf(hud_msg, sizeof(hud_msg), "%s", msg);
	hud_new_msg = 3;
}

static const char *base_name(const char *path)
{
	const char *p = strrchr(path, '/');
#ifdef _WIN32
	const char *q = strrchr(path, '\\');
	if (q != NULL && (p == NULL || q > p))
		p = q;
#endif
	return p != NULL ? p + 1 : path;
}

static int cmp_names(const void *a, const void *b)
{
	return strcasecmp((const char *)a, (const char *)b);
}

/* an .m3u next to the image that lists it: its lines are the set */
static int discs_from_m3u(const char *dir, const char *image)
{
	char path[MAXPATHLEN], line[MAXPATHLEN];
	struct dirent *ent;
	DIR *d = opendir(dir);
	int found = 0;

	if (d == NULL)
		return 0;
	while (!found && (ent = readdir(d)) != NULL) {
		const char *ext = strrchr(ent->d_name, '.');
		FILE *f;
		int n = 0, mine = -1;
		if (ext == NULL || strcasecmp(ext, ".m3u") != 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		f = fopen(path, "r");
		if (f == NULL)
			continue;
		while (n < AB_DISC_MAX && fgets(line, sizeof(line), f) != NULL) {
			char *e = line + strlen(line);
			while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
				*--e = 0;
			if (line[0] == 0 || line[0] == '#')
				continue;
			if (strcasecmp(base_name(line), image) == 0)
				mine = n;
			snprintf(discs[n], sizeof(discs[n]), "%s/%s", dir, base_name(line));
			n++;
		}
		fclose(f);
		if (mine >= 0 && n > 1) {
			disc_count = n;
			disc_current = mine;
			found = 1;
		}
	}
	closedir(d);
	return found;
}

/* the folder's images of the same kind as ours, in name order ("(Disc 1)" before "(Disc 2)") */
static int discs_from_folder(const char *dir, const char *image)
{
	const char *ext = strrchr(image, '.');
	char names[AB_DISC_MAX][MAXPATHLEN];
	struct dirent *ent;
	DIR *d;
	int n = 0, i;

	if (ext == NULL)
		return 0;
	d = opendir(dir);
	if (d == NULL)
		return 0;
	while (n < AB_DISC_MAX && (ent = readdir(d)) != NULL) {
		const char *e = strrchr(ent->d_name, '.');
		if (e == NULL || strcasecmp(e, ext) != 0)
			continue;
		snprintf(names[n++], MAXPATHLEN, "%s", ent->d_name);
	}
	closedir(d);
	if (n < 2)
		return 0;
	qsort(names, n, MAXPATHLEN, cmp_names);
	disc_current = 0;
	for (i = 0; i < n; i++) {
		snprintf(discs[i], sizeof(discs[i]), "%s/%s", dir, names[i]);
		if (strcasecmp(names[i], image) == 0)
			disc_current = i;
	}
	disc_count = n;
	return 1;
}

static void learn_set(void)
{
	const char *iso = GetIsoFile();
	char dir[MAXPATHLEN];
	const char *image;
	char *p;

	if (iso == NULL || iso[0] == 0)
		return;
	kind = DISCS_SINGLE;
	disc_count = 1;
	disc_current = 0;

	if (cdrIsoMultidiskCount > 1) {
		kind = DISCS_PBP;
		disc_count = cdrIsoMultidiskCount;
		disc_current = cdrIsoMultidiskSelect;
	} else {
		snprintf(dir, sizeof(dir), "%s", iso);
		image = base_name(iso);
		p = (char *)base_name(dir);
		if (p > dir)
			p[-1] = 0;
		else
			strcpy(dir, ".");
		if (discs_from_m3u(dir, image) || discs_from_folder(dir, image))
			kind = DISCS_FILES;
	}
	SysPrintf("autobleem: disc set: %d disc(s), this is %d\n", disc_count, disc_current + 1);
}

void ab_disc_tick(void)
{
	if (start_ms == 0)
		start_ms = plat_get_ticks_ms() ? plat_get_ticks_ms() : 1;
	if (kind == DISCS_NONE && CdromId[0] != 0)
		learn_set();

	/* the drive's tray, asked every ~100 calls by cdra_check_eject itself (1-2 ms each, not while a read
	 * is under way); a change is acted on between two CPU slices, never from here - inside a slice */
	if (cdra_is_physical() && !tray_pending) {
		int inserted;
		if (cdra_check_eject(&inserted) > 0 && !inserted != tray_open &&
		    emu_action == SACTION_NONE) {
			tray_now = !inserted;
			tray_pending = 1;
			ab_request_action(SACTION_AB_TRAY);
		}
	}
}

int ab_disc_in_drive(void)
{
	return cdra_is_physical();
}

void ab_disc_tray(void)
{
	tray_pending = 0;
	if (tray_now == tray_open)
		return;
	if (!tray_now) {
		/* closed, perhaps on another disc: the drive is opened again for it (its TOC, the subchannel
		 * probe) before the lid closes - the close's own CheckCdrom() then reads the new disc's id */
		CdromId[0] = 0;
		CdromLabel[0] = 0;
		cdra_close();
		if (cdra_open() < 0) {
			hud("DISC CHANGE FAILED");
			SysPrintf("autobleem: the drive's tray closed, the drive did not open again\n");
		} else
			hud("DISC INSERTED");
		SetCdOpenCaseTime(time(NULL) + 2);
	} else {
		/* open: the prefetch thread stops (it would read an empty drive); the lid stays open (-1) until
		 * the tray closes */
		cdra_stop_thread();
		SetCdOpenCaseTime(-1);
		hud("TRAY OPEN");
	}
	LidInterrupt();
	tray_open = tray_now;
	SysPrintf("autobleem: the drive's tray is %s\n", tray_open ? "open" : "closed");
}

int ab_disc_count(void)
{
	return disc_count;
}

int ab_disc_current(void)
{
	return disc_current;
}

int ab_disc_can_change(void)
{
	return plat_get_ticks_ms() - start_ms >= AB_OPEN_GRACE_S * 1000u;
}

int ab_disc_insert(int index)
{
	char msg[64];

	if (index < 0 || index >= disc_count)
		return -1;
	CdromId[0] = 0;
	CdromLabel[0] = 0;
	if (kind == DISCS_PBP) {
		cdrIsoMultidiskSelect = index;
		cdra_close();
		if (cdra_open() < 0) {
			hud("DISC CHANGE FAILED");
			return -1;
		}
	} else {
		set_cd_image(discs[index]);
		if (ReloadCdromPlugin() < 0 || cdra_open() < 0) {
			hud("DISC CHANGE FAILED");
			return -1;
		}
	}
	/* the lid opens for 2 s; on the close the core's lid sequence runs CheckCdrom() itself (the new
	 * disc's id and region) - doing it here as well, mid-sequence, is what crashed the game */
	SetCdOpenCaseTime(time(NULL) + 2);
	LidInterrupt();
	disc_current = index;

	snprintf(msg, sizeof(msg), "DISC %d OF %d INSERTED", index + 1, disc_count);
	hud(msg);
	SysPrintf("autobleem: %s (%s)\n", msg, kind == DISCS_PBP ? "pbp" : discs[index]);
	return 0;
}

void ab_disc_change(void)
{
	if (kind == DISCS_NONE)
		learn_set();
	ab_menu_change_disc();
}
