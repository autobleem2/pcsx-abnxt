/*
 * The emulator's own screens in the launcher's language: the disc picker and its two messages.
 *
 * The launcher starts pcsx-abnxt with -language <Name> (config.ini's language: English, Polski,
 * Chinese_Simplified, ... - the names of its own language files), and lang/<Name>.txt next to the emulator
 * (the launch script links it into the run directory like skin/) has the strings in that language, in the
 * same "English text=Translated text" format as AutoBleem's files. The text is rasterised by stb_truetype
 * straight into the menu's RGB565 canvas from skin/ui.ttf, or from the font a language file names with
 * "|@font|=<file>" (looked for in skin/ and fonts/ - the launcher's fonts folder, linked in by the script,
 * is where Chinese finds its Noto). Without a font the strings are English through libpicofe's 8x8 font.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PCSXAB_AB_UI_H
#define PCSXAB_AB_UI_H

enum ab_ui_str {
	AB_STR_CHANGE_DISC,	/* the picker's title */
	AB_STR_DISC,		/* "Disc" - "Disc 1", "Disc 2" under the icons */
	AB_STR_ONE_DISC,	/* "This game has only one disc" */
	AB_STR_NOT_NOW,		/* "You can't change discs now" */
	AB_STR_OK,
	AB_STR_SELECT,
	AB_STR_BACK,
	AB_STR_COUNT
};

/* the menu's look, skin/skin.cfg (docs/skin.md): colours as RGB565, the panels' and the selected row's
 * alpha, the background's and the font's file names in skin/. Read once, the first time it is asked for;
 * a missing file, key or a bad value keeps the built-in ab2.0.0 value. */
struct ab_skin {
	unsigned short text, dim, accent, panel, row, select_rim, name, shadow, grey;
	unsigned short hint_disc, hint_rim, hint_cross, hint_circle;
	int panel_alpha, row_alpha;
	char background[64], font[64];
};
const struct ab_skin *ab_ui_skin(void);

/* reads lang/<language>.txt and the font (the skin's, or the one the language file names), once; harmless
 * to call again */
void ab_ui_load(const char *language);
/* the string in the launcher's language (with a font to draw it), or the English default */
const char *ab_ui_str(enum ab_ui_str s);
/* any English text the language file translates (the menu's rows, values, help and messages); the text
 * itself when it has no translation or there is no font */
const char *ab_ui_tr(const char *en);
/* whether a font was loaded (else the caller draws with libpicofe's font) */
int ab_ui_has_font(void);

enum { AB_UI_LEFT, AB_UI_CENTER, AB_UI_RIGHT };

/* a canvas of RGB565 pixels, pitch in pixels */
struct ab_canvas {
	unsigned short *fb;
	int w, h, pitch;
};

/* text of `px` pixel height (the line box; the baseline follows the font's ascent), (x, y) the box's
 * top and its left/centre/right edge by `align`; returns the box's width */
int ab_ui_text(struct ab_canvas *c, int x, int y, int align, const char *utf8, int px, unsigned short rgb565);
int ab_ui_text_width(const char *utf8, int px);
/* the same text into an ARGB8888 buffer (w x h, pitch w), blended over what is there in argb's colour -
 * the HUD's images; returns the width drawn, 0 without a font */
int ab_ui_text_argb(unsigned int *px, int w, int h, int x, int y, const char *utf8, int size, unsigned int argb);

/* a disc: radius r about (cx, cy); accent = tinted (the one in the drive), dim = darker (not focused) */
void ab_ui_disc(struct ab_canvas *c, int cx, int cy, int r, int accent, int dim);
/* an anti-aliased ring of radius r and thickness t */
void ab_ui_ring(struct ab_canvas *c, int cx, int cy, int r, int t, unsigned short rgb565);
/* the pad's Cross and Circle, r the glyph's half size */
void ab_ui_cross(struct ab_canvas *c, int cx, int cy, int r, unsigned short rgb565);
void ab_ui_circle(struct ab_canvas *c, int cx, int cy, int r, unsigned short rgb565);

/* a filled rectangle with corners rounded by r, blended over the canvas at alpha (0..255) */
void ab_ui_fill(struct ab_canvas *c, int x, int y, int w, int h, int r, unsigned short rgb565, int alpha);
/* ab2.0.0's cut-corner shape (the launcher's and the installer's panels): the top right and bottom left
 * corners cut at 45 degrees by `cut` pixels, a rim `t` pixels thick in `rim` (opaque) around an inside of
 * `fill` blended at alpha; t = 0 draws no rim. One ab_ui_fill span per row, the rim and the inside never
 * overlapping, so a translucent inside does not show the rim through it. The diagonals are not smoothed. */
void ab_ui_cut_panel(struct ab_canvas *c, int x, int y, int w, int h, int cut, int t,
		     unsigned short rim, unsigned short fill, int alpha);

/* skin/ab_background.jpg (AutoBleem 2's launcher art, 1280x720) scaled to cover w x h pixels of `dst`
 * (pitch w; bilinear, centred, the overhang cropped); 0 and `dst` untouched when there is no image */
int ab_ui_background(unsigned short *dst, int w, int h);

#define AB_RGB565(r, g, b) ((unsigned short)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))

#endif
