/*
 * AutoBleem's in-game menu: what the PlayStation Classic player sees on the Home button (pcsx-ab's
 * e_menu_main3, "Enhanced Edition by AutoBleem Team"). Quick save/load, the disc, the filter, the whole
 * upstream menu one level down, the AutoBleem config, Exit.
 *
 * The entries are libpicofe menu_entry rows (the handlers, enums and ranges work as in every other menu)
 * but the screen is ours: AutoBleem 2's launcher art as the background (skin/ab_background.jpg), the game
 * and the build named in the launcher's font (ab_ui), the rows on a panel on the right. Nothing of the
 * paused game is shown - the frame libpicofe pasted behind its menu was garbage on the console whenever
 * the GPU rendered at another size than it reported. Upstream's own menus, one level down, keep their
 * look over a darkened copy of the same art.
 *
 * Not a translation unit of its own: frontend/menu.c #includes it after its own menus, the way it includes
 * libpicofe/menu.c, because everything a menu is built from (me_loop_d, mee_*, main_menu_handler,
 * menu_loop_savestate, menu_write_config...) is static in that unit.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "ab_disc.h"
#include "ab_buttons.h"
#include "ab_console.h"
#include "ab_debug.h"
#include "ab_ui.h"

/* our ids, past the menu.c enum's */
enum {
	MA_AB_QUICKSAVE = 1000,
	MA_AB_QUICKLOAD,
	MA_AB_DISC,
	MA_AB_FILTER,
	MA_AB_PCSX_MENU,
	MA_AB_SAVECFG,
};

#define AB_QUICK_SLOT 2		/* slot 0 is the resume point, 1 the launcher's copy of it */

static int ab_menu_handler(int id, int keys);
static int ab_menu_pcsx_handler(int id, int keys);
static int ab_disc_screen(void);
static const char *ab_filter_name(int id, int *offs);
static void ab_menu_prepare_bg(void);

static const char h_ab_filter[] = "Nearest = plain pixels, Linear = smoothed, Sharp = crisp pixels without"
                                  " shimmer, CRT = a TV's look";
static const char h_ab_pcsx[]   = "PCSX-ReARMed's own menu: options, controls, cheats...";
static const char h_ab_savecfg[] = "Keeps these settings for this game; AutoBleem shows its own locked until"
                                   " you unlock them in the game's settings";
static const char h_ab_scanlines[] = "Dark lines over the screen: 1 = every 2nd row, 2 = every 3rd,"
                                     " 3 = two of every 3; brightness is how dark (off with a CRT filter)";
/* upstream's soft_filter (the PCSX menu's "Software Filter"): scale2x/eagle2x on the GPU everywhere, our
 * hq2x/hq3x on the CPU (ab_scaler.c) - not offered on the console (ab_menu_loop_d picks the list) */
static const char *men_ab_smooth[] = { "None", "Scale2x", "Eagle2x", "HQ2x", "HQ3x", NULL };
static const char *men_ab_smooth_psc[] = { "None", "Scale2x", "Eagle2x", NULL };
static const char h_ab_smooth[]  = "Smooths 2D games' pixels before scaling (on the console: off with a CRT"
                                   " filter)";
/* the picture's shape as the launcher's "Widescreen" option sets it (-ratio): 4:3 in the middle of the
 * screen, or the whole 16:9 screen; the PCSX menu's "Scaler" is the full set, this is the switch */
static int ab_aspect_sel;
static const char *men_ab_aspect[] = { "4:3", "16:9 (fullscreen)", NULL };
static const char h_ab_aspect[]  = "4:3 as the PlayStation drew it, or stretched over the whole screen";
static const char h_ab_pad[]     = "Standard (digital), analog (DualShock), a gun or nothing;"
                                   " takes effect when the game goes on";
/* the Exit row's help, on every platform (ab_buttons.h) */
static const char h_ab_exit[]    = "Back to AutoBleem - holding the menu button for 2 seconds in the game"
                                   " does the same";

static menu_entry e_menu_ab[] =
{
	mee_handler_id("Resume game",              MA_MAIN_RESUME_GAME, main_menu_handler),
	mee_handler_id("Quick save",               MA_AB_QUICKSAVE,     ab_menu_handler),
	mee_handler_id("Quick load",               MA_AB_QUICKLOAD,     ab_menu_handler),
	mee_handler_id("Change disc",              MA_AB_DISC,          ab_menu_handler),
	mee_cust_h    ("Filter",                   MA_AB_FILTER,        ab_menu_handler, ab_filter_name, h_ab_filter),
	mee_enum_h    ("Smoothing",                MA_OPT_SWFILTER,     soft_filter, men_ab_smooth, h_ab_smooth),
	mee_enum_h    ("Screen",                   0,                   ab_aspect_sel, men_ab_aspect, h_ab_aspect),
	mee_enum_h    ("Scanlines",                MA_OPT_SCANLINES,    scanlines, men_scanlines, h_ab_scanlines),
	mee_range_h   ("Scanline brightness",      MA_OPT_SCANLINE_LEVEL, scanline_level, 0, 100, h_scanline_l),
	mee_enum_h    ("Controller 1",             0,                   in_type_sel1, men_in_type_sel, h_ab_pad),
	mee_enum_h    ("Controller 2",             0,                   in_type_sel2, men_in_type_sel, h_ab_pad),
	mee_handler_id_h("PCSX menu",              MA_AB_PCSX_MENU,     ab_menu_pcsx_handler, h_ab_pcsx),
	mee_handler_id_h("Save settings for this game", MA_AB_SAVECFG,  ab_menu_handler, h_ab_savecfg),
	mee_handler_id_h("Exit",                   MA_MAIN_EXIT,        main_menu_handler, NULL),
	mee_end,
};

/* the game's own config, pcsx.custom.cfg - what every save in these menus writes (ab_config.h) */
static int ab_save_config(void)
{
	return menu_write_config(1);
}

static int ab_menu_handler(int id, int keys)
{
	char msg[64];
	int ret;

	switch (id)
	{
	case MA_AB_QUICKSAVE:
		if (!ready_to_go || !CdromId[0])
			break;
		ret = emu_save_state(AB_QUICK_SLOT);
		snprintf(msg, sizeof(msg), ret == 0 ? "Quick save done" : "Quick save failed");
		menu_update_msg(msg);
		if (ret == 0)
			return 1;
		break;
	case MA_AB_QUICKLOAD:
		if (!ready_to_go || !CdromId[0])
			break;
		if (emu_check_state(AB_QUICK_SLOT) != 0) {
			menu_update_msg("No quick save yet");
			break;
		}
		ret = emu_load_state(AB_QUICK_SLOT);
		snprintf(msg, sizeof(msg), ret == 0 ? "Quick save loaded" : "Quick load failed");
		menu_update_msg(msg);
		if (ret == 0)
			return 1;
		break;
	case MA_AB_DISC:
		if (!ready_to_go || !CdromId[0])
			break;
		/* the same screens the Open button shows; a disc put in resumes the game */
		if (ab_disc_screen())
			return 1;
		break;
	case MA_AB_FILTER:
		/* a value row: Left goes back through the list, Right and Cross forward */
		if (plat_target.hwfilters == NULL)
			break;
		if (keys & PBTN_LEFT) {
			if (plat_target.hwfilter > 0)
				plat_target.hwfilter--;
			else
				while (plat_target.hwfilters[plat_target.hwfilter + 1] != NULL)
					plat_target.hwfilter++;
		} else {
			plat_target.hwfilter++;
			if (plat_target.hwfilters[plat_target.hwfilter] == NULL)
				plat_target.hwfilter = 0;
		}
		break;
	case MA_AB_SAVECFG:
		menu_update_msg(ab_save_config() == 0 ? "Saved for this game" : "Failed to save the settings");
		break;
	default:
		break;
	}
	return 0;
}

static const char *ab_filter_name(int id, int *offs)
{
	return plat_target.hwfilters != NULL ? plat_target.hwfilters[plat_target.hwfilter] : "-";
}

/* ---- the disc picker (docs/port-plan.md, phase 5) ----
 *
 * Drawn on the menu's screen (the art, the bar - "the menu screen" below), at a 1280x720 design scaled
 * to the canvas: the title top left, the set's discs in a row on a panel (the one in the drive in
 * AutoBleem's cyan, the focused one bright with a ring, the rest dimmed), "Disc n" under each, and
 * Cross/Circle hints on the bar. The text is the launcher's language through ab_ui (the built-in 8x8
 * font, in English, when there is no ui font). Sony's picker started on the next disc and so does this
 * one: Open, Cross is the common case. */

static const unsigned short ab_col_text   = AB_RGB565(0xf4, 0xf6, 0xf8);
static const unsigned short ab_col_dim    = AB_RGB565(0x9a, 0xa4, 0xb2);
static const unsigned short ab_col_accent = AB_RGB565(0x4f, 0xc3, 0xf7);
static const unsigned short ab_col_panel  = AB_RGB565(0x04, 0x12, 0x30);
static const unsigned short ab_col_row    = AB_RGB565(0x1a, 0x7e, 0xc4);
static const unsigned short ab_col_name   = AB_RGB565(0xd6, 0xdd, 0xe6);
static const unsigned short ab_col_shadow = AB_RGB565(0x00, 0x08, 0x1c);

static unsigned short *ab_bg;		/* the art at the canvas' size, bright (ab_menu_prepare_bg) */
static int ab_bg_w, ab_bg_h;

static struct ab_canvas ab_canvas(void)
{
	struct ab_canvas c = { g_menuscreen_ptr, g_menuscreen_w, g_menuscreen_h, g_menuscreen_pp };
	return c;
}

static int ab_text_width(const char *s, int px)
{
	return ab_ui_has_font() ? ab_ui_text_width(s, px) : (int)strlen(s) * me_mfont_w;
}

/* returns the text's width */
static int ab_text(struct ab_canvas *c, int x, int y, int align, const char *s, int px, unsigned short col)
{
	int w;

	if (ab_ui_has_font())
		return ab_ui_text(c, x, y, align, s, px, col);
	w = ab_text_width(s, px);
	if (align == AB_UI_CENTER)
		x -= w / 2;
	else if (align == AB_UI_RIGHT)
		x -= w;
	text_out16(x, y + (px - me_mfont_h) / 2, "%s", s);
	return w;
}

/* a screen of ours begins: the canvas with the art on it */
static struct ab_canvas ab_screen_begin(void)
{
	struct ab_canvas c;
	int y;

	menu_draw_begin(0, 1);
	c = ab_canvas();
	if (ab_bg != NULL && ab_bg_w == c.w && ab_bg_h == c.h)
		for (y = 0; y < c.h; y++)
			memcpy(c.fb + (size_t)y * c.pitch, ab_bg + (size_t)y * c.w, c.w * 2);
	else
		memset(c.fb, 0, (size_t)c.h * c.pitch * 2);
	return c;
}

/* text with a soft dark shadow behind it, for the parts drawn straight over the art */
static void ab_text_shadow(struct ab_canvas *c, int x, int y, int align, const char *s, int px, unsigned short col)
{
	int d = px >= 30 ? 2 : 1;
	ab_text(c, x + d, y + d, align, s, px, ab_col_shadow);
	ab_text(c, x, y, align, s, px, col);
}

/* a pad glyph and its text, left to right from *x, which moves past them */
static void ab_hint(struct ab_canvas *c, int *x, int y, int px, int is_cross, const char *text)
{
	float s = c->h / 720.0f;
	int r = (int)(12 * s), gap = (int)(10 * s);

	if (is_cross)
		ab_ui_cross(c, *x + r, y + px / 2, r, ab_col_accent);
	else
		ab_ui_circle(c, *x + r, y + px / 2, r, ab_col_accent);
	*x += 2 * r + gap;
	*x += ab_text(c, *x, y, AB_UI_LEFT, text, px, ab_col_text) + (int)(36 * s);
}

/* "(x) ok   (o) back" on the art's bar, from its left end; back == NULL for a plain "(x) OK" */
static void ab_footer(struct ab_canvas *c, const char *ok, const char *back)
{
	float s = c->h / 720.0f;
	int px = (int)(22 * s), x = (int)(490 * s), y = (int)(647 * s);

	ab_hint(c, &x, y, px, 1, ok);
	if (back != NULL)
		ab_hint(c, &x, y, px, 0, back);
}

static void ab_draw_disc_picker(int n, int cur, int sel)
{
	struct ab_canvas c;
	float s;
	int r, gap, step, x0, cy, i;
	char label[80];

	ab_debug_screen("disc");
	c = ab_screen_begin();
	s = c.h / 720.0f;
	ab_text_shadow(&c, (int)(40 * s), (int)(40 * s), AB_UI_LEFT, ab_ui_str(AB_STR_CHANGE_DISC), (int)(36 * s), ab_col_text);
	if (CdromId[0] != 0)
		ab_text_shadow(&c, (int)(40 * s), (int)(86 * s), AB_UI_LEFT, get_cd_label(), (int)(22 * s), ab_col_dim);

	r = (int)(62 * s);
	gap = (int)(58 * s);
	step = 2 * r + gap;
	x0 = (c.w - (n * 2 * r + (n - 1) * gap)) / 2 + r;
	cy = (int)(c.h * 0.46f);
	/* the discs on a panel, as the menu's rows are */
	ab_ui_fill(&c, x0 - r - (int)(60 * s), cy - r - (int)(50 * s), (n - 1) * step + 2 * r + (int)(120 * s),
		   2 * r + (int)(130 * s), (int)(14 * s), ab_col_panel, 210);
	for (i = 0; i < n; i++) {
		int cx = x0 + i * step;
		ab_ui_disc(&c, cx, cy, r, i == cur, i != sel);
		if (i == sel)
			ab_ui_ring(&c, cx, cy, r + (int)(9 * s), (int)(4 * s) > 1 ? (int)(4 * s) : 1, ab_col_accent);
		snprintf(label, sizeof(label), "%s %d", ab_ui_str(AB_STR_DISC), i + 1);
		ab_text(&c, cx, cy + r + (int)(20 * s), AB_UI_CENTER, label, (int)(26 * s),
			i == sel ? ab_col_text : ab_col_dim);
	}
	ab_footer(&c, ab_ui_str(AB_STR_SELECT), ab_ui_str(AB_STR_BACK));
	menu_draw_end();
}

static void ab_draw_message(const char *msg)
{
	struct ab_canvas c;
	float s;

	int px, w;

	ab_debug_screen("message");
	c = ab_screen_begin();
	s = c.h / 720.0f;
	px = (int)(32 * s);
	w = ab_text_width(msg, px) + (int)(120 * s);
	ab_ui_fill(&c, (c.w - w) / 2, (int)(c.h * 0.44f) - (int)(40 * s), w, px + (int)(80 * s), (int)(14 * s), ab_col_panel, 210);
	ab_text(&c, c.w / 2, (int)(c.h * 0.44f), AB_UI_CENTER, msg, px, ab_col_text);
	ab_footer(&c, ab_ui_str(AB_STR_OK), NULL);
	menu_draw_end();
}

/* the buttons that got us here are not the screen's */
static void ab_wait_released(void)
{
	while (in_menu_wait_any(NULL, 50) & (PBTN_MOK|PBTN_MBACK|PBTN_MENU)) {
		if (ab_console_power_off_requested)
			break;
	}
}

static void ab_message_screen(const char *msg)
{
	int inp;

	ab_draw_message(msg);
	ab_wait_released();
	for (;;) {
		inp = in_menu_wait(PBTN_MOK|PBTN_MBACK|PBTN_MENU, NULL, 70);
		if (ab_console_power_off_requested || (inp & (PBTN_MOK|PBTN_MBACK|PBTN_MENU)))
			break;
		if (inp & PBTN_RDRAW)
			ab_draw_message(msg);
	}
}

/* in the menu's context; 1 when a disc went in (the game should go on), 0 otherwise */
static int ab_disc_screen(void)
{
	int n, cur, sel, inp;

	ab_ui_load(ab_opts.language);
	n = ab_disc_count();
	cur = ab_disc_current();
	if (!ab_disc_can_change() || n == 0) {
		ab_message_screen(ab_ui_str(AB_STR_NOT_NOW));
		return 0;
	}
	if (n <= 1) {
		ab_message_screen(ab_ui_str(AB_STR_ONE_DISC));
		return 0;
	}
	sel = (cur + 1) % n;
	ab_draw_disc_picker(n, cur, sel);
	ab_wait_released();
	for (;;) {
		inp = in_menu_wait(PBTN_LEFT|PBTN_RIGHT|PBTN_MOK|PBTN_MBACK|PBTN_MENU, NULL, 70);
		if (ab_console_power_off_requested || (inp & (PBTN_MBACK|PBTN_MENU)))
			return 0;
		if (inp & PBTN_MOK) {
			if (sel == cur)
				return 0;	/* the disc that is in already: nothing to do */
			return ab_disc_insert(sel) == 0;
		}
		if ((inp & PBTN_LEFT) && sel > 0)
			sel--;
		if ((inp & PBTN_RIGHT) && sel < n - 1)
			sel++;
		ab_draw_disc_picker(n, cur, sel);
	}
}

/* the Open button's path: from the running game and back to it (ab_disc_change) */
void ab_menu_change_disc(void)
{
	menu_leave_emu();
	ab_menu_prepare_bg();
	in_set_config_int(0, IN_CFG_BLOCKING, 1);
	ab_disc_screen();
	ab_wait_released();
	in_set_config_int(0, IN_CFG_BLOCKING, 0);
	menu_prepare_emu();
}

/* ---- the menu screen ----
 *
 * A 1280x720 design scaled by the canvas' height, the way the picker is: AutoBleem 2's launcher art
 * behind everything (its logo bottom left, its bar along the bottom), the game's name and id top left
 * with the selected row's help (or the last message) under them, the rows on a translucent panel on the
 * right, the pad hints and the build on the bar. Without the art (no skin/) the same over plain navy. */

/* the art at the canvas' size: ours as it is, libpicofe's background (what its own menus, the picker
 * and the message screens draw over) darkened; made again when the window changed size. Every menu
 * entry: menu_leave_emu() has just pasted the game's last frame into g_menubg_ptr, which is what the
 * console showed as garbage - it is covered here. */
static void ab_menu_prepare_bg(void)
{
	int w = g_menuscreen_w, h = g_menuscreen_h, i;

	if (ab_bg == NULL || ab_bg_w != w || ab_bg_h != h) {
		free(ab_bg);
		ab_bg = calloc((size_t)w * h, 2);
		if (ab_bg == NULL) {
			ab_bg_w = ab_bg_h = 0;
			return;
		}
		ab_bg_w = w;
		ab_bg_h = h;
		if (!ab_ui_background(ab_bg, w, h)) {
			/* plain navy, a shade lighter towards the top */
			for (i = 0; i < h; i++) {
				unsigned short col = AB_RGB565(0x06, 0x1c + 0x14 * (h - i) / h, 0x4c + 0x20 * (h - i) / h);
				int x;
				for (x = 0; x < w; x++)
					ab_bg[(size_t)i * w + x] = col;
			}
		}
		menu_darken_bg(g_menubg_src_ptr, ab_bg, w * h, 0);
	}
	memcpy(g_menubg_ptr, g_menubg_src_ptr, (size_t)w * h * 2);
}

/* `s` word-wrapped to `width` pixels, at most `lines` lines; returns the lines drawn */
static int ab_text_wrap(struct ab_canvas *c, int x, int y, const char *s, int px, int width, int lines, unsigned short col)
{
	char line[256];
	int n = 0;

	while (*s != 0 && n < lines) {
		const char *p = s, *cut = NULL;
		int len;
		/* the longest prefix that fits, broken at a space; a word wider than the line goes whole */
		for (;;) {
			const char *q = strchr(p, ' ');
			if (q == NULL)
				q = p + strlen(p);
			len = (int)(q - s);
			if (len >= (int)sizeof(line))
				len = sizeof(line) - 1;
			memcpy(line, s, len);
			line[len] = 0;
			if (ab_text_width(line, px) > width && cut != NULL)
				break;
			cut = q;
			if (*q == 0)
				break;
			p = q + 1;
		}
		len = (int)(cut - s);
		if (len >= (int)sizeof(line))
			len = sizeof(line) - 1;
		memcpy(line, s, len);
		line[len] = 0;
		ab_text_shadow(c, x, y, AB_UI_LEFT, line, px, col);
		y += px + px / 4;
		n++;
		s = cut;
		while (*s == ' ')
			s++;
	}
	return n;
}

static const char *ab_cpu_name(void)
{
	if (Config.Cpu == CPU_INTERPRETER)
		return "interpreter";
#if defined(LIGHTREC)
	return "lightrec";
#elif !defined(DRC_DISABLE)
	return sizeof(void *) == 8 ? "ARM64 dynarec" : "ARM dynarec";
#else
	return "interpreter";
#endif
}

static const char *ab_gpu_name(void)
{
	if (strcmp(Config.Gpu, "builtin_gpu") != 0)
		return Config.Gpu;
#if defined(BUILTIN_GPU_NEON)
	return "NEON GPU";
#elif defined(BUILTIN_GPU_PEOPS)
	return "P.E.Op.S GPU";
#else
	return "Unai GPU";
#endif
}

static void ab_menu_draw(const menu_entry *menu, int sel)
{
	struct ab_canvas c;
	const menu_entry *ent, *ent_sel = NULL;
	char buf[128], ltime_s[16];
	time_t ltime;
	float s;
	int n, i, y, x, px, row_h, pad, panel_x, panel_y, panel_w, panel_h, x_name, x_val;

	ab_debug_screen("menu");
	c = ab_screen_begin();
	s = c.h / 720.0f;

	/* top left: the game */
	x = (int)(40 * s);
	y = (int)(40 * s);
	if (CdromId[0] != 0) {
		ab_text_shadow(&c, x, y, AB_UI_LEFT, get_cd_label(), (int)(36 * s), ab_col_text);
		y += (int)(46 * s);
		ltime = time(NULL);
		strftime(ltime_s, sizeof(ltime_s), "%H:%M", localtime(&ltime));
		snprintf(buf, sizeof(buf), "%.9s  \xc2\xb7  %s  \xc2\xb7  %s  \xc2\xb7  %s", CdromId,
			 Config.PsxType ? "PAL" : "NTSC", Config.HLE ? "HLE BIOS" : "BIOS", ltime_s);
		ab_text_shadow(&c, x, y, AB_UI_LEFT, buf, (int)(22 * s), ab_col_dim);
	} else {
		ab_text_shadow(&c, x, y, AB_UI_LEFT, "pcsx-abnxt", (int)(36 * s), ab_col_text);
	}

	/* the rows, on the panel */
	for (n = 0, ent = menu, i = 0; ent->name; ent++, i++) {
		if (!ent->enabled)
			continue;
		if (i == sel)
			ent_sel = ent;
		n++;
	}
	if (ent_sel != NULL)
		menu_sel_name = ent_sel->name;	/* as libpicofe's menus do, for the debug driver */
	px = (int)(24 * s);
	row_h = (int)(34 * s);
	pad = (int)(18 * s);
	panel_w = (int)(560 * s);
	panel_x = c.w - (int)(40 * s) - panel_w;
	panel_y = (int)(40 * s);
	panel_h = n * row_h + 2 * pad;
	ab_ui_fill(&c, panel_x, panel_y, panel_w, panel_h, (int)(14 * s), ab_col_panel, 210);
	x_name = panel_x + (int)(30 * s);
	x_val = panel_x + panel_w - (int)(30 * s);
	y = panel_y + pad;
	for (ent = menu, i = 0; ent->name; ent++, i++) {
		const char *name = ent->name, *val = NULL;
		int offs = 0, is_sel = i == sel;

		if (!ent->enabled)
			continue;
		if (is_sel)
			ab_ui_fill(&c, panel_x + (int)(12 * s), y, panel_w - (int)(24 * s), row_h, (int)(8 * s), ab_col_row, 170);
		if (name[0] == 0 && ent->generate_name != NULL)
			name = ent->generate_name(ent->id, &offs);
		switch (ent->beh) {
		case MB_OPT_ONOFF:
			val = me_read_onoff(ent) ? "ON" : "OFF";
			break;
		case MB_OPT_RANGE:
			snprintf(buf, sizeof(buf), "%d", *(int *)ent->var);
			val = buf;
			break;
		case MB_OPT_ENUM:
			val = ((const char **)ent->data)[*(int *)ent->var];
			break;
		case MB_OPT_CUSTOM:
		case MB_OPT_CUSTONOFF:
		case MB_OPT_CUSTRANGE:
			if (ent->generate_name != NULL)
				val = ent->generate_name(ent->id, &offs);
			break;
		default:
			break;
		}
		ab_text(&c, x_name, y + (row_h - px) / 2, AB_UI_LEFT, name, px, is_sel ? ab_col_text : ab_col_name);
		if (val != NULL) {
			int vx = x_val, vy = y + (row_h - px) / 2;
			if (is_sel) {
				/* the arrows of a value row, so Left/Right is obvious */
				vx -= ab_text(&c, x_val, vy, AB_UI_RIGHT, ">", px, ab_col_accent) + (int)(8 * s);
				vx -= ab_text(&c, vx, vy, AB_UI_RIGHT, val, px, ab_col_text) + (int)(8 * s);
				ab_text(&c, vx, vy, AB_UI_RIGHT, "<", px, ab_col_accent);
			} else {
				ab_text(&c, vx, vy, AB_UI_RIGHT, val, px, ab_col_dim);
			}
		}
		y += row_h;
	}

	/* the message of the moment, else the selected row's help, under the game's name */
	x = (int)(40 * s);
	y = (int)(150 * s);
	if (menu_error_msg[0] != 0) {
		ab_text_wrap(&c, x, y, menu_error_msg, (int)(24 * s), panel_x - x - (int)(40 * s), 2, ab_col_accent);
		if (plat_get_ticks_ms() - menu_error_time > 2048)
			menu_error_msg[0] = 0;
	} else if (ent_sel != NULL && ent_sel->help != NULL) {
		ab_text_wrap(&c, x, y, ent_sel->help, (int)(20 * s), panel_x - x - (int)(40 * s), 3, ab_col_dim);
	}

	/* the bar: the hints on its left, the build on its right */
	ab_footer(&c, "Select", ready_to_go ? "Resume" : "Back");
	px = (int)(17 * s);
	x = c.w - (int)(40 * s);
	/* the package's version (AB_VERSION, exported by the launcher) - what every program on the stick shows;
	   the emulator's own git describe only when it was started without the launcher */
	{
		const char *version = getenv("AB_VERSION");
		if (version && *version)
			snprintf(buf, sizeof(buf), "AutoBleem %s", version);
		else
			snprintf(buf, sizeof(buf), "pcsx-abnxt %s", REV[0] != 0 ? REV : "(no version)");
	}
	ab_text(&c, x, (int)(636 * s), AB_UI_RIGHT, buf, px, ab_col_text);
	snprintf(buf, sizeof(buf), "PCSX-ReARMed  \xc2\xb7  %s  \xc2\xb7  %s  \xc2\xb7  built %s", ab_cpu_name(), ab_gpu_name(), __DATE__);
	ab_text(&c, x, (int)(660 * s), AB_UI_RIGHT, buf, px, ab_col_dim);

	menu_draw_end();
}

/* me_loop_d() over our screen: the same keys, the same handler contract (1 from a handler = leave the
 * menu, which is the game going on or the run ending) */
static int ab_menu_run(menu_entry *menu, int *menu_sel)
{
	int ret = 0, inp, sel = *menu_sel, sel_max;

	sel_max = me_count(menu) - 1;
	if (sel_max < 0)
		return 0;
	while ((!menu[sel].enabled || !menu[sel].selectable) && sel < sel_max)
		sel++;

	ab_menu_draw(menu, sel);
	ab_wait_released();
	for (;;) {
		ab_menu_draw(menu, sel);
		inp = in_menu_wait(PBTN_UP|PBTN_DOWN|PBTN_LEFT|PBTN_RIGHT|
			PBTN_MOK|PBTN_MBACK|PBTN_MENU|PBTN_L|PBTN_R, NULL, 70);
		if (ab_console_power_off_requested || (inp & (PBTN_MENU|PBTN_MBACK)))
			break;
		if (inp & PBTN_UP) {
			do {
				if (--sel < 0)
					sel = sel_max;
			} while (!menu[sel].enabled || !menu[sel].selectable);
		}
		if (inp & PBTN_DOWN) {
			do {
				if (++sel > sel_max)
					sel = 0;
			} while (!menu[sel].enabled || !menu[sel].selectable);
		}
		if (inp & (PBTN_LEFT|PBTN_RIGHT|PBTN_L|PBTN_R)) {
			if (me_process(&menu[sel], (inp & (PBTN_RIGHT|PBTN_R)) ? 1 : 0, inp & (PBTN_L|PBTN_R)))
				continue;
		}
		if (inp & (PBTN_MOK|PBTN_LEFT|PBTN_RIGHT|PBTN_L|PBTN_R)) {
			/* a plain row takes Cross alone; a value row with a handler takes the arrows too */
			if (menu[sel].handler != NULL && (menu[sel].beh != MB_NONE || (inp & PBTN_MOK))) {
				ret = menu[sel].handler(menu[sel].id, inp);
				if (ret)
					break;
				sel_max = me_count(menu) - 1;
			}
		}
	}
	*menu_sel = sel;
	return ret;
}

static int ab_menu_pcsx_handler(int id, int keys)
{
	static int sel = 0;

	me_enable(e_menu_main, MA_MAIN_RESUME_GAME, ready_to_go);
	me_enable(e_menu_main, MA_MAIN_SAVE_STATE,  ready_to_go && CdromId[0]);
	me_enable(e_menu_main, MA_MAIN_LOAD_STATE,  ready_to_go && CdromId[0]);
	me_enable(e_menu_main, MA_MAIN_RESET_GAME,  ready_to_go);
	me_enable(e_menu_main, MA_MAIN_CHEATS,      ready_to_go && NumCheats);

	ab_debug_screen("pcsx");	/* upstream's menu and its pages, until we draw ours again */
	return me_loop_d(e_menu_main, &sel, NULL, draw_frame_main);
}

/* menu_loop()'s loop: our menu at the top, until the game is to go on or the run is to end */
static void ab_menu_loop_d(void)
{
	static int sel = 0;

	me_enable(e_menu_ab, MA_MAIN_RESUME_GAME, ready_to_go);
	me_enable(e_menu_ab, MA_AB_QUICKSAVE, ready_to_go && CdromId[0]);
	me_enable(e_menu_ab, MA_AB_QUICKLOAD, ready_to_go && CdromId[0]);
	me_enable(e_menu_ab, MA_AB_DISC,      ready_to_go && CdromId[0]);
	me_enable(e_menu_ab, MA_AB_FILTER,    plat_target.hwfilters != NULL);
	e_menu_ab[me_id2offset(e_menu_ab, MA_MAIN_EXIT)].help = h_ab_exit;
	if (ab_console_present()) {
		e_menu_ab[me_id2offset(e_menu_ab, MA_OPT_SWFILTER)].data = men_ab_smooth_psc;
		if (soft_filter > SOFT_FILTER_EAGLE2X)
			soft_filter = SOFT_FILTER_NONE;
	}

	ab_ui_load(ab_opts.language);
	ab_menu_prepare_bg();
	do {
		ab_aspect_sel = g_scaler == SCALE_FULLSCREEN;
		ab_menu_run(e_menu_ab, &sel);
		/* the row is a two-way switch over g_scaler; a scaler the row cannot name (custom, 1x1) is left
		 * alone unless the row was moved */
		if (ab_aspect_sel != (g_scaler == SCALE_FULLSCREEN))
			g_scaler = ab_aspect_sel ? SCALE_FULLSCREEN : SCALE_4_3;
	} while (!ready_to_go && !g_emu_want_quit);
}
