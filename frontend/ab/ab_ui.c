/*
 * The emulator's own screens in the launcher's language - see ab_ui.h.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
/* the menu's background: a JPEG (a PNG would be six times the file for this art) */
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include "../../libpcsxcore/system.h"
#include "../main.h"
#include "ab_ui.h"

/* the keys as the launcher's English.txt has them - what lang/<Name>.txt translates */
static const char *english[AB_STR_COUNT] = {
	"Change disc",
	"Disc",
	"This game has only one disc",
	"You can't change discs now",
	"OK",
	"Select",
	"Back",
};

/* where skin/, lang/ and fonts/ are: the run directory first - AutoBleem's launch scripts link them there
 * next to .pcsx/, bios/ and plugins/, and copy the binary to /tmp/pcsx, so the executable's own directory
 * (upstream's data dir, emu_make_data_path) is only right for a build run from where it was built */
static void data_path(char *path, size_t size, const char *end)
{
	if (access(end, R_OK) == 0) {
		snprintf(path, size, "%s", end);
		return;
	}
	emu_make_data_path(path, end, size);
}

/* ---- the skin: skin/skin.cfg (docs/skin.md) ----
 *
 * The menu's look as data, so it can follow the launcher's theme: the colours, the panels' alpha, and the
 * background's and the font's file names in skin/. The built-in values are ab2.0.0's (autobleem-design
 * themes/ab2.0.0/design/emu/README.md) - the same as the shipped file - and stay for a missing file, a
 * missing key or a bad value. Read once, the first time anything asks for it; never per frame. */

static struct ab_skin skin = {
	.text        = AB_RGB565(0xf4, 0xf6, 0xf8),
	.dim         = AB_RGB565(0x9a, 0xa4, 0xb2),
	.accent      = AB_RGB565(0x36, 0xd9, 0xe0),
	.panel       = AB_RGB565(0x26, 0x2e, 0x38),
	.row         = AB_RGB565(0x48, 0x1a, 0x36),
	.select_rim  = AB_RGB565(0xff, 0x46, 0xaa),
	.name        = AB_RGB565(0xd6, 0xdd, 0xe6),
	.shadow      = AB_RGB565(0x0c, 0x0f, 0x13),
	.grey        = AB_RGB565(0x5c, 0x66, 0x74),
	.hint_disc   = AB_RGB565(0x3a, 0x3a, 0x3e),
	.hint_rim    = AB_RGB565(0x59, 0x59, 0x5d),
	.hint_cross  = AB_RGB565(0x6d, 0x7d, 0xf6),
	.hint_circle = AB_RGB565(0xe4, 0x4e, 0x74),
	.panel_alpha = 220,
	.row_alpha   = 200,
	.text_scale  = 110,
	.hud_scale   = 110,
	.background  = "ab_background.jpg",
	.font        = "ui.ttf",
};
static int skin_read;

static const struct { const char *key; size_t off; } skin_colours[] = {
	{ "text", offsetof(struct ab_skin, text) },
	{ "dim", offsetof(struct ab_skin, dim) },
	{ "accent", offsetof(struct ab_skin, accent) },
	{ "panel", offsetof(struct ab_skin, panel) },
	{ "row", offsetof(struct ab_skin, row) },
	{ "select_rim", offsetof(struct ab_skin, select_rim) },
	{ "name", offsetof(struct ab_skin, name) },
	{ "shadow", offsetof(struct ab_skin, shadow) },
	{ "grey", offsetof(struct ab_skin, grey) },
	{ "hint_disc", offsetof(struct ab_skin, hint_disc) },
	{ "hint_rim", offsetof(struct ab_skin, hint_rim) },
	{ "hint_cross", offsetof(struct ab_skin, hint_cross) },
	{ "hint_circle", offsetof(struct ab_skin, hint_circle) },
};

static const struct { const char *key; size_t off; int lo, hi; } skin_ints[] = {
	{ "panel_alpha", offsetof(struct ab_skin, panel_alpha), 0, 255 },
	{ "row_alpha", offsetof(struct ab_skin, row_alpha), 0, 255 },
	{ "text_scale", offsetof(struct ab_skin, text_scale), 50, 200 },
	{ "hud_scale", offsetof(struct ab_skin, hud_scale), 50, 200 },
};

static unsigned int skin_int(const char *key)
{
	unsigned int i;

	for (i = 0; i < sizeof(skin_ints) / sizeof(skin_ints[0]); i++)
		if (strcmp(key, skin_ints[i].key) == 0)
			break;
	return i;
}

static char *trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t')
		s++;
	for (e = s + strlen(s); e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'); e--)
		e[-1] = 0;
	return s;
}

/* "RRGGBB" or "#RRGGBB" */
static int parse_colour(const char *v, unsigned short *out)
{
	unsigned long rgb;
	char *end;

	if (*v == '#')
		v++;
	if (strlen(v) != 6)
		return 0;
	rgb = strtoul(v, &end, 16);
	if (*end != 0)
		return 0;
	*out = AB_RGB565((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
	return 1;
}

/* a file name in skin/, not a path */
static int parse_file(const char *v, char *out, size_t size)
{
	if (v[0] == 0 || v[0] == '.' || strchr(v, '/') != NULL || strchr(v, '\\') != NULL || strlen(v) >= size)
		return 0;
	snprintf(out, size, "%s", v);
	return 1;
}

static void skin_line(char *line, const char *path, int n)
{
	char *eq, *key, *val;
	unsigned int i;
	int ok = 0;

	key = trim(line);
	if (key[0] == '#' || key[0] == 0)
		return;
	eq = strchr(key, '=');
	if (eq == NULL) {
		SysPrintf("autobleem: %s:%d: not key = value, left out\n", path, n);
		return;
	}
	*eq = 0;
	key = trim(key);
	val = trim(eq + 1);
	for (i = 0; i < sizeof(skin_colours) / sizeof(skin_colours[0]); i++)
		if (strcmp(key, skin_colours[i].key) == 0)
			break;
	if (i < sizeof(skin_colours) / sizeof(skin_colours[0]))
		ok = parse_colour(val, (unsigned short *)((char *)&skin + skin_colours[i].off));
	else if ((i = skin_int(key)) < sizeof(skin_ints) / sizeof(skin_ints[0])) {
		char *end;
		long v = strtol(val, &end, 10);
		if ((ok = val[0] != 0 && *end == 0 && v >= skin_ints[i].lo && v <= skin_ints[i].hi))
			*(int *)((char *)&skin + skin_ints[i].off) = (int)v;
	} else if (strcmp(key, "background") == 0)
		ok = parse_file(val, skin.background, sizeof(skin.background));
	else if (strcmp(key, "font") == 0)
		ok = parse_file(val, skin.font, sizeof(skin.font));
	else {
		SysPrintf("autobleem: %s:%d: unknown key %s, left out\n", path, n, key);
		return;
	}
	if (!ok)
		SysPrintf("autobleem: %s:%d: bad value for %s (%s), the built-in one stays\n", path, n, key, val);
}

const struct ab_skin *ab_ui_skin(void)
{
	char path[MAXPATHLEN], line[256];
	FILE *f;
	int n = 0;

	if (skin_read)
		return &skin;
	skin_read = 1;
	data_path(path, sizeof(path), "skin/skin.cfg");
	f = fopen(path, "r");
	if (f == NULL) {
		SysPrintf("autobleem: no %s, the menu has its built-in look\n", path);
		return &skin;
	}
	while (fgets(line, sizeof(line), f) != NULL)
		skin_line(line, path, ++n);
	fclose(f);
	SysPrintf("autobleem: menu skin from %s\n", path);
	return &skin;
}

/* every "English=Translated" pair of the language file (the menu, its help, its messages, the HUD's) */
static struct { char *en, *tr; } *pairs;
static int npairs, pairs_cap;
static char font_name[128];		/* |@font| from the language file, a file in skin/ or fonts/ */
static unsigned char *font_data;
static stbtt_fontinfo font;
static int font_ok, loaded;

static void take_line(char *line)
{
	char *eq = strchr(line, '=');
	char *e;

	if (line[0] == '#' || eq == NULL)
		return;
	*eq++ = 0;
	for (e = eq + strlen(eq); e > eq && (e[-1] == '\n' || e[-1] == '\r'); e--)
		e[-1] = 0;
	if (*eq == 0)
		return;
	if (strcmp(line, "|@font|") == 0) {
		snprintf(font_name, sizeof(font_name), "%s", eq);
		return;
	}
	if (npairs == pairs_cap) {
		int cap = pairs_cap ? pairs_cap * 2 : 64;
		void *n = realloc(pairs, cap * sizeof(*pairs));
		if (n == NULL)
			return;
		pairs = n;
		pairs_cap = cap;
	}
	pairs[npairs].en = strdup(line);
	pairs[npairs].tr = strdup(eq);
	if (pairs[npairs].en != NULL && pairs[npairs].tr != NULL)
		npairs++;
}

const char *ab_ui_tr(const char *en)
{
	int i;

	/* a translation can only be drawn with a real font: the 8x8 one is ASCII */
	if (en == NULL || !font_ok)
		return en;
	for (i = 0; i < npairs; i++)
		if (strcmp(pairs[i].en, en) == 0)
			return pairs[i].tr;
	return en;
}

static void read_strings(const char *language)
{
	char path[MAXPATHLEN], line[512];
	char end[160];
	FILE *f;

	if (language == NULL || language[0] == 0 || strchr(language, '/') != NULL)
		return;
	snprintf(end, sizeof(end), "lang/%s.txt", language);
	data_path(path, sizeof(path), end);
	f = fopen(path, "r");
	if (f == NULL) {
		SysPrintf("autobleem: no %s, the emulator's screens are in English\n", path);
		return;
	}
	while (fgets(line, sizeof(line), f) != NULL)
		take_line(line);
	fclose(f);
	SysPrintf("autobleem: ui strings from %s\n", path);
}

static int load_font_file(const char *end)
{
	char path[MAXPATHLEN];
	FILE *f;
	long size;

	data_path(path, sizeof(path), end);
	f = fopen(path, "rb");
	if (f == NULL)
		return 0;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0 || size > 64 * 1024 * 1024) {
		fclose(f);
		return 0;
	}
	font_data = malloc(size);
	if (font_data == NULL || fread(font_data, 1, size, f) != (size_t)size) {
		fclose(f);
		free(font_data);
		font_data = NULL;
		return 0;
	}
	fclose(f);
	if (!stbtt_InitFont(&font, font_data, stbtt_GetFontOffsetForIndex(font_data, 0))) {
		SysPrintf("autobleem: %s is not a font stb_truetype reads\n", path);
		free(font_data);
		font_data = NULL;
		return 0;
	}
	SysPrintf("autobleem: ui font %s\n", path);
	return 1;
}

void ab_ui_load(const char *language)
{
	char end[192];

	if (loaded)
		return;
	loaded = 1;
	read_strings(language);
	if (font_name[0] != 0) {
		snprintf(end, sizeof(end), "skin/%s", font_name);
		font_ok = load_font_file(end);
		if (!font_ok) {
			snprintf(end, sizeof(end), "fonts/%s", font_name);
			font_ok = load_font_file(end);
		}
		if (!font_ok)
			SysPrintf("autobleem: the font %s the language file names is not in skin/ or fonts/\n", font_name);
	}
	if (!font_ok) {
		snprintf(end, sizeof(end), "skin/%s", ab_ui_skin()->font);
		font_ok = load_font_file(end);
		if (!font_ok && strcmp(ab_ui_skin()->font, "ui.ttf") != 0) {
			SysPrintf("autobleem: the skin's font %s is not in skin/, trying ui.ttf\n", ab_ui_skin()->font);
			font_ok = load_font_file("skin/ui.ttf");
		}
	}
	if (!font_ok)
		SysPrintf("autobleem: no ui font, the emulator's screens use the built-in font, in English\n");
}

const char *ab_ui_str(enum ab_ui_str s)
{
	if (s < 0 || s >= AB_STR_COUNT)
		return "";
	return ab_ui_tr(english[s]);
}

int ab_ui_has_font(void)
{
	return font_ok;
}

/* ---- drawing into RGB565 ---- */

static inline void blend(unsigned short *p, int r, int g, int b, int a)
{
	int dr, dg, db;

	if (a <= 0)
		return;
	if (a >= 255) {
		*p = AB_RGB565(r, g, b);
		return;
	}
	dr = (*p >> 8) & 0xf8;
	dg = (*p >> 3) & 0xfc;
	db = (*p << 3) & 0xf8;
	dr += (r - dr) * a / 255;
	dg += (g - dg) * a / 255;
	db += (b - db) * a / 255;
	*p = AB_RGB565(dr, dg, db);
}

static inline void plot(struct ab_canvas *c, int x, int y, int r, int g, int b, int a)
{
	if (x < 0 || y < 0 || x >= c->w || y >= c->h)
		return;
	blend(c->fb + y * c->pitch + x, r, g, b, a);
}

static void unpack(unsigned short rgb565, int *r, int *g, int *b)
{
	*r = (rgb565 >> 8) & 0xf8;
	*g = (rgb565 >> 3) & 0xfc;
	*b = (rgb565 << 3) & 0xf8;
}

static int utf8_next(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	int cp, extra, i;

	if (p[0] < 0x80) {
		cp = p[0];
		extra = 0;
	} else if ((p[0] & 0xe0) == 0xc0) {
		cp = p[0] & 0x1f;
		extra = 1;
	} else if ((p[0] & 0xf0) == 0xe0) {
		cp = p[0] & 0x0f;
		extra = 2;
	} else if ((p[0] & 0xf8) == 0xf0) {
		cp = p[0] & 0x07;
		extra = 3;
	} else {
		*s += 1;
		return '?';
	}
	for (i = 1; i <= extra; i++) {
		if ((p[i] & 0xc0) != 0x80) {
			*s += i;
			return '?';
		}
		cp = (cp << 6) | (p[i] & 0x3f);
	}
	*s += extra + 1;
	return cp;
}

int ab_ui_ascii(const char *utf8, char *out, int size)
{
	/* U+00C0..U+00FF's base letters ('?' for the signs among them) */
	static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs" "aaaaaaaceeeeiiiidnooooo-ouuuuyty";
	const char *s = utf8, *rep;
	char one[2] = { 0, 0 };
	int cp, n = 0;

	if (size <= 0)
		return 0;
	while (s != NULL && (cp = utf8_next(&s)) != 0) {
		rep = one;
		if (cp >= 0x20 && cp < 0x7f)
			one[0] = (char)cp;
		else if (cp >= 0xc0 && cp <= 0xff)
			one[0] = latin1[cp - 0xc0];
		else if (cp == 0xb7)
			rep = "-";
		else if (cp == 0xa9)
			rep = "(c)";
		else if (cp == '\n' || cp == '\t' || cp == 0xa0)
			rep = " ";
		else
			rep = "?";
		for (; *rep != 0 && n < size - 1; rep++)
			out[n++] = *rep;
	}
	out[n] = 0;
	return n;
}

int ab_ui_text_width(const char *utf8, int px)
{
	float scale, w = 0;
	const char *s = utf8;
	int cp, adv, lsb;

	if (!font_ok)
		return 0;
	scale = stbtt_ScaleForPixelHeight(&font, px);
	while ((cp = utf8_next(&s)) != 0) {
		stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
		w += adv * scale;
	}
	return (int)(w + 0.5f);
}

int ab_ui_text(struct ab_canvas *c, int x, int y, int align, const char *utf8, int px, unsigned short rgb565)
{
	float scale, pen;
	const char *s = utf8;
	int cp, ascent, descent, gap, width, r, g, b;

	if (!font_ok)
		return 0;
	width = ab_ui_text_width(utf8, px);
	if (align == AB_UI_CENTER)
		x -= width / 2;
	else if (align == AB_UI_RIGHT)
		x -= width;
	scale = stbtt_ScaleForPixelHeight(&font, px);
	stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
	unpack(rgb565, &r, &g, &b);
	pen = x;
	while ((cp = utf8_next(&s)) != 0) {
		int adv, lsb, gw, gh, gx, gy, gi, row, col;
		unsigned char *bitmap;

		gi = stbtt_FindGlyphIndex(&font, cp);
		stbtt_GetGlyphHMetrics(&font, gi, &adv, &lsb);
		bitmap = stbtt_GetGlyphBitmapSubpixel(&font, scale, scale, pen - (int)pen, 0, gi, &gw, &gh, &gx, &gy);
		if (bitmap != NULL) {
			int top = y + (int)(ascent * scale + 0.5f) + gy;
			int left = (int)pen + gx;
			for (row = 0; row < gh; row++)
				for (col = 0; col < gw; col++)
					plot(c, left + col, top + row, r, g, b, bitmap[row * gw + col]);
			stbtt_FreeBitmap(bitmap, NULL);
		}
		pen += adv * scale;
	}
	return width;
}

/* the text into an ARGB8888 buffer (w x h, pitch w): the glyphs' coverage over what is there, in `argb`'s
 * colour - for the HUD, whose images are blended over the screen */
int ab_ui_text_argb(unsigned int *px, int w, int h, int x, int y, const char *utf8, int size, unsigned int argb)
{
	float scale, pen;
	const char *s = utf8;
	int cp, ascent, descent, gap;
	unsigned int ca = argb >> 24, rgb = argb & 0xffffff;

	if (!font_ok)
		return 0;
	scale = stbtt_ScaleForPixelHeight(&font, size);
	stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
	pen = x;
	while ((cp = utf8_next(&s)) != 0) {
		int adv, lsb, gw, gh, gx, gy, gi, row, col;
		unsigned char *bitmap;

		gi = stbtt_FindGlyphIndex(&font, cp);
		stbtt_GetGlyphHMetrics(&font, gi, &adv, &lsb);
		bitmap = stbtt_GetGlyphBitmapSubpixel(&font, scale, scale, pen - (int)pen, 0, gi, &gw, &gh, &gx, &gy);
		if (bitmap != NULL) {
			int top = y + (int)(ascent * scale + 0.5f) + gy;
			int left = (int)pen + gx;
			for (row = 0; row < gh; row++) {
				for (col = 0; col < gw; col++) {
					int tx = left + col, ty = top + row;
					unsigned int a = bitmap[row * gw + col] * ca / 255, *d, da, oa;
					if (a == 0 || tx < 0 || ty < 0 || tx >= w || ty >= h)
						continue;
					d = px + ty * w + tx;
					da = *d >> 24;
					oa = a + da * (255 - a) / 255;
					if (oa == 0)
						continue;
					/* "over" in straight alpha, per channel */
					{
						unsigned int sr = (rgb >> 16) & 255, sg = (rgb >> 8) & 255, sb = rgb & 255;
						unsigned int dr = (*d >> 16) & 255, dg = (*d >> 8) & 255, db = *d & 255;
						unsigned int k = da * (255 - a) / 255;
						dr = (sr * a + dr * k) / oa;
						dg = (sg * a + dg * k) / oa;
						db = (sb * a + db * k) / oa;
						*d = (oa << 24) | (dr << 16) | (dg << 8) | db;
					}
				}
			}
			stbtt_FreeBitmap(bitmap, NULL);
		}
		pen += adv * scale;
	}
	return (int)(pen - x + 0.5f);
}

/* coverage of a point at distance d from an edge at radius r: 1 inside, 0 outside, a pixel wide ramp */
static inline float edge(float r, float d)
{
	float v = r + 0.5f - d;
	return v < 0 ? 0 : v > 1 ? 1 : v;
}

void ab_ui_disc(struct ab_canvas *c, int cx, int cy, int r, int accent, int dim)
{
	float hole = r * 0.22f, ring = r * 0.30f;
	int x, y;

	for (y = -r - 1; y <= r + 1; y++) {
		for (x = -r - 1; x <= r + 1; x++) {
			float d = sqrtf((float)(x * x + y * y));
			float cov = edge((float)r, d) * (1 - edge(hole, d));
			float t, shine;
			int cr, cg, cb;

			if (cov <= 0)
				continue;
			/* the data area: light silver, lighter towards the hub, a soft sheen across it */
			t = (d - hole) / (r - hole);
			if (t < 0)
				t = 0;
			shine = 0.5f + 0.5f * cosf(atan2f((float)y, (float)x) * 2 + 0.8f);
			cr = cg = cb = (int)(232 - 42 * t + 18 * shine);
			if (accent) {
				/* the disc in the drive: AutoBleem's cyan on it */
				cr = (int)(cr * 0.45f);
				cg = (int)(cg * 0.85f);
			}
			/* the clear ring around the hub */
			if (d < ring) {
				cr = (cr * 3 + 60) / 4;
				cg = (cg * 3 + 70) / 4;
				cb = (cb * 3 + 80) / 4;
			}
			/* the rim */
			if (d > r - 2.5f) {
				cr = cr * 2 / 3;
				cg = cg * 2 / 3;
				cb = cb * 2 / 3;
			}
			if (dim) {
				cr = cr * 11 / 20;
				cg = cg * 11 / 20;
				cb = cb * 11 / 20;
			}
			plot(c, cx + x, cy + y, cr, cg, cb, (int)(cov * 255));
		}
	}
}

void ab_ui_ring(struct ab_canvas *c, int cx, int cy, int r, int t, unsigned short rgb565)
{
	int x, y, cr, cg, cb, lim = r + t;
	float half = t / 2.0f;

	unpack(rgb565, &cr, &cg, &cb);
	for (y = -lim; y <= lim; y++) {
		for (x = -lim; x <= lim; x++) {
			float d = fabsf(sqrtf((float)(x * x + y * y)) - r);
			float cov = edge(half, d);
			if (cov > 0)
				plot(c, cx + x, cy + y, cr, cg, cb, (int)(cov * 255));
		}
	}
}

/* an anti-aliased line of thickness t from (x0, y0) to (x1, y1) */
static void line(struct ab_canvas *c, float x0, float y0, float x1, float y1, float t, int cr, int cg, int cb)
{
	float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
	int xmin = (int)floorf((x0 < x1 ? x0 : x1) - t), xmax = (int)ceilf((x0 > x1 ? x0 : x1) + t);
	int ymin = (int)floorf((y0 < y1 ? y0 : y1) - t), ymax = (int)ceilf((y0 > y1 ? y0 : y1) + t);
	int x, y;

	for (y = ymin; y <= ymax; y++) {
		for (x = xmin; x <= xmax; x++) {
			float u = len2 > 0 ? ((x - x0) * dx + (y - y0) * dy) / len2 : 0;
			float px, py, d, cov;
			if (u < 0)
				u = 0;
			if (u > 1)
				u = 1;
			px = x0 + u * dx;
			py = y0 + u * dy;
			d = sqrtf((x - px) * (x - px) + (y - py) * (y - py));
			cov = edge(t / 2, d);
			if (cov > 0)
				plot(c, x, y, cr, cg, cb, (int)(cov * 255));
		}
	}
}

void ab_ui_cross(struct ab_canvas *c, int cx, int cy, int r, unsigned short rgb565)
{
	int cr, cg, cb;
	float a = r * 0.62f, t = r * 0.22f;

	unpack(rgb565, &cr, &cg, &cb);
	ab_ui_ring(c, cx, cy, r, r * 0.18f > 1 ? (int)(r * 0.18f) : 1, rgb565);
	line(c, cx - a, cy - a, cx + a, cy + a, t, cr, cg, cb);
	line(c, cx - a, cy + a, cx + a, cy - a, t, cr, cg, cb);
}

void ab_ui_circle(struct ab_canvas *c, int cx, int cy, int r, unsigned short rgb565)
{
	ab_ui_ring(c, cx, cy, r, r * 0.18f > 1 ? (int)(r * 0.18f) : 1, rgb565);
	ab_ui_ring(c, cx, cy, (int)(r * 0.55f), r * 0.2f > 1 ? (int)(r * 0.2f) : 1, rgb565);
}

void ab_ui_pad_glyph(struct ab_canvas *c, int cx, int cy, int r, int is_cross,
		     unsigned short disc, unsigned short rim, unsigned short mark)
{
	float t = r * 0.07f > 1 ? r * 0.07f : 1;
	int x, y, dr, dg, db, mr, mg, mb;

	/* the disc, its edge smoothed */
	unpack(disc, &dr, &dg, &db);
	for (y = -r - 1; y <= r + 1; y++)
		for (x = -r - 1; x <= r + 1; x++) {
			float cov = edge((float)r, sqrtf((float)(x * x + y * y)));
			if (cov > 0)
				plot(c, cx + x, cy + y, dr, dg, db, (int)(cov * 255));
		}
	ab_ui_ring(c, cx, cy, (int)(r - t / 2 + 0.5f), (int)(t + 0.5f), rim);
	/* the mark, in the launcher's default glyphs' proportions (30 px: a cross of +-6.5, a ring of radius 6.5) */
	unpack(mark, &mr, &mg, &mb);
	if (is_cross) {
		float a = r * 0.42f, w = r * 0.2f > 1.5f ? r * 0.2f : 1.5f;
		line(c, cx - a, cy - a, cx + a, cy + a, w, mr, mg, mb);
		line(c, cx - a, cy + a, cx + a, cy - a, w, mr, mg, mb);
	} else {
		int w = r * 0.16f > 1 ? (int)(r * 0.16f + 0.5f) : 1;
		ab_ui_ring(c, cx, cy, (int)(r * 0.45f + 0.5f), w, mark);
	}
}

void ab_ui_fill(struct ab_canvas *c, int x, int y, int w, int h, int r, unsigned short rgb565, int alpha)
{
	int cr, cg, cb, px, py;

	if (alpha <= 0 || w <= 0 || h <= 0)
		return;
	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;
	unpack(rgb565, &cr, &cg, &cb);
	for (py = 0; py < h; py++) {
		int cy = py < r ? r - py : py >= h - r ? py - (h - r - 1) : 0;
		for (px = 0; px < w; px++) {
			int cx = px < r ? r - px : px >= w - r ? px - (w - r - 1) : 0;
			int a = alpha;
			/* a pixel in one of the corner squares: coverage by its distance from the corner's centre */
			if (cx > 0 && cy > 0) {
				float cov = edge((float)r, sqrtf((float)(cx * cx + cy * cy)));
				if (cov <= 0)
					continue;
				a = (int)(alpha * cov);
			}
			plot(c, x + px, y + py, cr, cg, cb, a);
		}
	}
}

void ab_ui_cut_panel(struct ab_canvas *c, int x, int y, int w, int h, int cut, int t,
		     unsigned short rim, unsigned short fill, int alpha)
{
	int row, ic;

	if (w <= 0 || h <= 0)
		return;
	if (cut > w / 2)
		cut = w / 2;
	if (cut > h / 2)
		cut = h / 2;
	if (cut < 0)
		cut = 0;
	if (t < 0)
		t = 0;
	/* the inside's cut: a 45 degree edge moved in by t across itself moves t * sqrt(2) along a row, of
	 * which the inset takes t - so the inside's diagonal starts (sqrt(2) - 1) * t further in */
	ic = cut - (t * 586 + 500) / 1000;
	if (ic < 0)
		ic = 0;
	for (row = 0; row < h; row++) {
		/* the outline's insets on this row, left (the bottom left cut) and right (the top right one) */
		int ol = cut - (h - 1 - row), or_ = cut - row, il, ir;
		if (ol < 0)
			ol = 0;
		if (or_ < 0)
			or_ = 0;
		if (row < t || row >= h - t || w - 2 * t <= 0) {
			ab_ui_fill(c, x + ol, y + row, w - ol - or_, 1, 0, rim, 255);
			continue;
		}
		/* the inside's, in the same coordinates */
		il = ic - (h - t - 1 - row);
		ir = ic - (row - t);
		il = t + (il > 0 ? il : 0);
		ir = t + (ir > 0 ? ir : 0);
		if (il < ol)
			il = ol;
		if (ir < or_)
			ir = or_;
		if (t > 0) {
			ab_ui_fill(c, x + ol, y + row, il - ol, 1, 0, rim, 255);
			ab_ui_fill(c, x + w - ir, y + row, ir - or_, 1, 0, rim, 255);
		}
		ab_ui_fill(c, x + il, y + row, w - il - ir, 1, 0, fill, alpha);
	}
}

/* the file whole, NULL when it is not there */
static unsigned char *read_file(const char *path, long *size)
{
	unsigned char *data;
	FILE *f = fopen(path, "rb");

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = *size > 0 && *size <= 64 * 1024 * 1024 ? malloc(*size) : NULL;
	if (data != NULL && fread(data, 1, *size, f) != (size_t)*size) {
		free(data);
		data = NULL;
	}
	fclose(f);
	return data;
}

int ab_ui_background(unsigned short *dst, int w, int h)
{
	static int missing;
	char path[MAXPATHLEN], end[96];
	unsigned char *file, *img;
	long size;
	int iw, ih, n, x, y;
	/* 16.16 fixed point: the source step per output pixel and the start of the crop */
	int step, sx0, sy0;

	if (missing || w <= 0 || h <= 0)
		return 0;
	snprintf(end, sizeof(end), "skin/%s", ab_ui_skin()->background);
	data_path(path, sizeof(path), end);
	file = read_file(path, &size);
	if (file == NULL && strcmp(ab_ui_skin()->background, "ab_background.jpg") != 0) {
		SysPrintf("autobleem: no %s, trying skin/ab_background.jpg\n", path);
		data_path(path, sizeof(path), "skin/ab_background.jpg");
		file = read_file(path, &size);
	}
	if (file == NULL) {
		SysPrintf("autobleem: no %s, the menu has a plain background\n", path);
		missing = 1;
		return 0;
	}
	img = stbi_load_from_memory(file, (int)size, &iw, &ih, &n, 3);
	free(file);
	if (img == NULL) {
		SysPrintf("autobleem: %s: %s\n", path, stbi_failure_reason());
		missing = 1;
		return 0;
	}
	/* cover: the axis that needs more scaling sets the factor, the other overhangs and is cropped */
	if ((long)iw * h > (long)ih * w)
		step = (ih << 16) / h;
	else
		step = (iw << 16) / w;
	sx0 = ((iw << 16) - step * w) / 2;
	sy0 = ((ih << 16) - step * h) / 2;
	for (y = 0; y < h; y++) {
		int sy = sy0 + y * step, iy = sy >> 16, fy = (sy >> 8) & 0xff;
		const unsigned char *r0, *r1;
		if (iy < 0) { iy = 0; fy = 0; }
		if (iy >= ih - 1) { iy = ih - 1; fy = 0; }
		r0 = img + (size_t)iy * iw * 3;
		r1 = iy + 1 < ih ? r0 + iw * 3 : r0;
		for (x = 0; x < w; x++) {
			int sx = sx0 + x * step, ix = sx >> 16, fx = (sx >> 8) & 0xff, ix1, c, rgb[3];
			if (ix < 0) { ix = 0; fx = 0; }
			if (ix >= iw - 1) { ix = iw - 1; fx = 0; }
			ix1 = ix + 1 < iw ? ix + 1 : ix;
			for (c = 0; c < 3; c++) {
				int top = r0[ix * 3 + c] * (256 - fx) + r0[ix1 * 3 + c] * fx;
				int bot = r1[ix * 3 + c] * (256 - fx) + r1[ix1 * 3 + c] * fx;
				rgb[c] = (top * (256 - fy) + bot * fy) >> 16;
			}
			dst[(size_t)y * w + x] = AB_RGB565(rgb[0], rgb[1], rgb[2]);
		}
	}
	stbi_image_free(img);
	return 1;
}
