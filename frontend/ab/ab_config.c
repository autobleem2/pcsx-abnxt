/*
 * AutoBleem's command line and configuration - see ab_config.h.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libpcsxcore/psxcommon.h"
#include "../../libpcsxcore/sio.h"
#include "../libpicofe/plat.h"
#include "../menu.h"
#include "../main.h"
#include "ab_config.h"
#include "ab_hacks.h"

/* the BIOS files AutoBleem keeps in System/Bios (the launcher's launch.sh links it as bios/) */
#define AB_BIOS_WORLD "romw.bin"
#define AB_BIOS_JAPAN "romJP.bin"

static int bios_auto;	/* the BIOS came from "SET_BY_PCSX" and nobody chose another since */

struct ab_options ab_opts = {
	.filter = 0,
	.ratio = 0,
	.lang = 2,	/* English (US) */
	.region = 4,
	.enter = 1,
	.display = 1,
	.language = "English",
	.fullscreen = 0,
	.dotdir = NULL,
	.biosdir = NULL,
};

static int take_str(const char *name, char *value, size_t size, int argc, char *argv[], int *i)
{
	if (strcmp(argv[*i], name) != 0)
		return 0;
	if (*i + 1 >= argc) {
		fprintf(stderr, "%s needs a value\n", name);
		return 1;
	}
	snprintf(value, size, "%s", argv[++*i]);
	return 1;
}

static int take_string(const char *name, const char **value, int argc, char *argv[], int *i)
{
	if (strcmp(argv[*i], name) != 0)
		return 0;
	if (*i + 1 >= argc) {
		fprintf(stderr, "%s needs a value\n", name);
		return 1;
	}
	*value = argv[++*i];
	return 1;
}

static int take_flag(const char *name, int *value, char *argv[], int *i)
{
	if (strcmp(argv[*i], name) != 0)
		return 0;
	*value = 1;
	return 1;
}

void ab_make_path(char *buf, size_t size, const char *home, const char *dir, const char *fname)
{
	size_t dotlen = strlen(PCSX_DOT_DIR);

	if (ab_opts.dotdir != NULL && strncmp(dir, PCSX_DOT_DIR, dotlen) == 0) {
		/* the profile folder given outright: "/.pcsx/" and what is under it become <dotdir>/... */
		snprintf(buf, size, "%s/%s%s", ab_opts.dotdir, dir + dotlen, fname ? fname : "");
		return;
	}
	if (fname)
		snprintf(buf, size, "%s%s%s", home, dir, fname);
	else
		snprintf(buf, size, "%s%s", home, dir);
}

const char *ab_gameid_format(const char *fmt, char *out, size_t size, const char *home)
{
	size_t dotlen = strlen(PCSX_DOT_DIR);

	if (ab_opts.dotdir != NULL && strncmp(fmt, "%s" PCSX_DOT_DIR, 2 + dotlen) == 0) {
		snprintf(out, size, "%%s/%s", fmt + 2 + dotlen);
		return ab_opts.dotdir;
	}
	snprintf(out, size, "%s", fmt);
	return home;
}

static int take_int(const char *name, int *value, int lo, int hi, int argc, char *argv[], int *i)
{
	int v;

	if (strcmp(argv[*i], name) != 0)
		return 0;
	if (*i + 1 >= argc) {
		fprintf(stderr, "%s needs a value\n", name);
		return 1;
	}
	v = atoi(argv[++*i]);
	if (v < lo || v > hi) {
		fprintf(stderr, "%s %d is out of range (%d..%d), ignored\n", name, v, lo, hi);
		return 1;
	}
	*value = v;
	return 1;
}

int ab_args_take(int argc, char *argv[])
{
	int i, out = 1;

	/* the log is read after a crash or a kill more often than not: a whole line reaches it at once. Line
	 * buffering is full buffering on the Windows CRT, so there it is unbuffered; elsewhere a write per
	 * character was a system call per character into the log (AutoBleem's quiet-stick plan) */
#ifdef _WIN32
	setvbuf(stdout, NULL, _IONBF, 0);
#else
	setvbuf(stdout, NULL, _IOLBF, 0);
#endif
	setvbuf(stderr, NULL, _IONBF, 0);

	for (i = 1; i < argc; i++) {
		if (take_int("-filter", &ab_opts.filter, 0, 2, argc, argv, &i)
		    || take_int("-ratio", &ab_opts.ratio, 0, 1, argc, argv, &i)
		    || take_int("-lang", &ab_opts.lang, 1, 13, argc, argv, &i)
		    || take_int("-region", &ab_opts.region, 1, 4, argc, argv, &i)
		    || take_int("-enter", &ab_opts.enter, 0, 2, argc, argv, &i)
		    || take_int("-display", &ab_opts.display, 0, 1, argc, argv, &i)
		    || take_str("-language", ab_opts.language, sizeof(ab_opts.language), argc, argv, &i)
		    || take_flag("-fullscreen", &ab_opts.fullscreen, argv, &i)
		    || take_flag("-sonyhacks", &ab_opts.sonyhacks, argv, &i)
		    || take_string("-dotdir", &ab_opts.dotdir, argc, argv, &i)
		    || take_string("-biosdir", &ab_opts.biosdir, argc, argv, &i))
			continue;
		argv[out++] = argv[i];
	}
	argv[out] = NULL;
	/* a trailing separator on the folders would double up in the paths built from them */
	if (ab_opts.dotdir != NULL) {
		char *d = strdup(ab_opts.dotdir);
		size_t n = strlen(d);
		while (n > 1 && (d[n - 1] == '/' || d[n - 1] == '\\'))
			d[--n] = 0;
		ab_opts.dotdir = d;
	}
	printf("autobleem: filter=%d ratio=%d lang=%d region=%d language=%s fullscreen=%d sonyhacks=%d dotdir=%s biosdir=%s\n",
		ab_opts.filter, ab_opts.ratio, ab_opts.lang, ab_opts.region, ab_opts.language, ab_opts.fullscreen,
		ab_opts.sonyhacks, ab_opts.dotdir ? ab_opts.dotdir : "-", ab_opts.biosdir ? ab_opts.biosdir : "-");
	return out;
}

/* 1 when the game's own config has "<key> = " at the start of a line */
static int custom_has_key(const char *key)
{
	char path[MAXPATHLEN], line[256];
	size_t n = strlen(key);
	int found = 0;
	FILE *f;

	emu_make_path(path, sizeof(path), PCSX_DOT_DIR, AB_CUSTOM_CFG);
	f = fopen(path, "r");
	if (f == NULL)
		return 0;
	while (!found && fgets(line, sizeof(line), f) != NULL)
		found = strncmp(line, key, n) == 0 && strncmp(line + n, " = ", 3) == 0;
	fclose(f);
	return found;
}

/* the launcher's hand-overs through the environment (the abfeatures file next to the binary says which
 * this build takes); NULL when not given */
static const char *ab_env(const char *name)
{
	const char *v = getenv(name);
	return v != NULL && v[0] != 0 ? v : NULL;
}

const char *ab_exit_dir(void)
{
	return ab_env("AB_EXIT_DIR");
}

const char *ab_memcard_dir(void)
{
	return ab_env("AB_MEMCARD_DIR");
}

const char *ab_load_state(void)
{
	return ab_env("AB_LOAD_STATE");
}

void ab_pad_order(int order[2])
{
	const char *v = ab_env("AB_PAD_ORDER");
	int a = -1, b = -1;

	order[0] = 0;
	order[1] = 1;
	if (v == NULL)
		return;
	if (sscanf(v, "%d,%d", &a, &b) != 2)
		return;
	if ((a != 0 && a != 1) || (b != 0 && b != 1) || a == b)
		return;
	order[0] = a;
	order[1] = b;
}

int ab_bios_set_by_pcsx(void)
{
	return bios_auto && strcmp(Config.Bios[0], AB_BIOS_WORLD) == 0;
}

void ab_config_loaded(int is_game)
{
	if (strcmp(Config.Bios[0], AB_BIOS_SET_BY_PCSX) == 0) {
		snprintf(Config.Bios[PSX_REGION_US], sizeof(Config.Bios[0]), "%s", AB_BIOS_WORLD);
		snprintf(Config.Bios[PSX_REGION_EU], sizeof(Config.Bios[0]), "%s", AB_BIOS_WORLD);
		snprintf(Config.Bios[PSX_REGION_JP], sizeof(Config.Bios[0]), "%s", AB_BIOS_JAPAN);
		bios_auto = 1;
	} else if (strcmp(Config.Bios[0], AB_BIOS_WORLD) != 0) {
		bios_auto = 0;	/* a BIOS of the file's own choosing (a game config without the key keeps ours) */
	}

	/* the default card2.mcd: AutoBleem's memory-card sets are one card, swapped in as card1.mcd */
	if (strstr(Config.Mcd2, "card2.mcd") != NULL) {
		strcpy(Config.Mcd2, "none");
		LoadMcds(Config.Mcd1, Config.Mcd2);
	}
	/* $AB_MEMCARD_DIR (abfeatures: memcarddir): the launcher's memory-card set for this game, played from
	 * where it is - Games/!MemCards/<set>/card1.mcd - instead of copied into the game's folder and back */
	if (ab_memcard_dir() != NULL) {
		char mcd[sizeof(Config.Mcd1)];
		snprintf(mcd, sizeof(mcd), "%s/card1.mcd", ab_memcard_dir());
		if (strcmp(mcd, Config.Mcd1) != 0) {
			snprintf(Config.Mcd1, sizeof(Config.Mcd1), "%s", mcd);
			LoadMcds(Config.Mcd1, Config.Mcd2);
		}
	}

	/* the launcher's per-launch choices beat the file's - but not the game's own config's */
	if (plat_target.hwfilters != NULL && !(is_game && custom_has_key("plat_target.hwfilter")))
		plat_target.hwfilter = ab_opts.filter;	/* hwfilters[] = { "Off", "Linear", "Sharp" }: the launcher sends 0/1 */
	if (!(is_game && custom_has_key("g_scaler3")))
		g_scaler = ab_opts.ratio ? SCALE_FULLSCREEN : SCALE_4_3;
	fprintf(stderr, "autobleem: %s config: filter=%s ratio=%s boot logo=%s scanlines=%d\n",
		is_game ? "game" : "global",
		plat_target.hwfilters != NULL ? plat_target.hwfilters[plat_target.hwfilter] : "-",
		g_scaler == SCALE_FULLSCREEN ? "16:9" : "4:3", Config.SlowBoot ? "shown" : "skipped", scanlines);
	if (is_game)
		ab_hacks_apply();	/* -sonyhacks: Sony's overrides for this serial, over the file (ab_hacks.h) */
}
