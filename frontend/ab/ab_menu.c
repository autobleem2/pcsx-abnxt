/*
 * AutoBleem's in-game menu: what the PlayStation Classic player sees on the Home button (pcsx-ab's
 * e_menu_main3, "Enhanced Edition by AutoBleem Team"). Quick save/load, the disc, the filter, the whole
 * upstream menu one level down, the AutoBleem config, Exit.
 *
 * The entries are libpicofe menu_entry rows (the handlers, enums and ranges work as in every other menu)
 * but the screen is ours: AutoBleem 2's launcher art as the background (skin/ab_background.jpg), the game
 * and the build named in the launcher's font (ab_ui), the rows on a panel on the left. Nothing of the
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

#include <strings.h>

#include "ab_disc.h"
#include "ab_buttons.h"
#include "ab_console.h"
#include "ab_debug.h"
#include "ab_ui.h"
#include "ab_pad_battery.h"
#include "ab_shaders.h"
#include "../../libpcsxcore/state_sony.h"

/* our ids, past the menu.c enum's */
enum {
	MA_AB_QUICKSAVE = 1000,
	MA_AB_QUICKLOAD,
	MA_AB_DISC,
	MA_AB_FILTER,
	MA_AB_PCSX_MENU,
	MA_AB_SAVECFG,
	MA_AB_SCALER,
	MA_AB_AUTOLOAD,
	MA_AB_DISPLAY,
	MA_AB_ENHANCE,
	MA_AB_NOSEAMS,
	MA_AB_DITHER,
};

#define AB_QUICK_SLOT 2		/* slot 0 is the resume point, 1 the launcher's copy of it */

static int ab_menu_handler(int id, int keys);
static int ab_menu_pcsx_handler(int id, int keys);
static int ab_disc_screen(void);
static const char *ab_filter_name(int id, int *offs);
static void ab_menu_prepare_bg(void);

/* (the texts are the language files' keys: no '=' in them) */
static const char h_ab_filter[] = "Nearest: plain pixels. Linear: smoothed. Sharp: crisp pixels without"
                                  " shimmer. CRT: a TV's look";
static const char h_ab_crtpi_1080[] = "CRT-Pi is too heavy for the console at 1080p: choose 720p in AutoBleem's"
                                      " settings to play with it";
static const char h_ab_pcsx[]   = "PCSX-ReARMed's own menu: options, controls, cheats...";
static const char h_ab_savecfg[] = "Keeps these settings for this game; AutoBleem shows its own locked until"
                                   " you unlock them in the game's settings";
static const char h_ab_scanlines[] = "Dark lines over the screen: 1 is every 2nd row, 2 every 3rd, 3 two of"
                                     " every 3; brightness is how dark (off with a CRT filter)";
/* upstream's soft_filter (the PCSX menu's "Software Filter"): scale2x/eagle2x on the GPU everywhere, our
 * hq2x/hq3x on the CPU (ab_scaler.c) - not offered on the console (ab_menu_loop_d picks the list) */
static const char *men_ab_smooth[] = { "None", "Scale2x", "Eagle2x", "HQ2x", "HQ3x", NULL };
static const char *men_ab_smooth_psc[] = { "None", "Scale2x", "Eagle2x", NULL };
static const char h_ab_smooth[]  = "Smooths 2D games' pixels before scaling";
/* the scaler, as upstream's g_scaler (the PCSX menu's "Scaler" without "custom", which is left alone
 * unless this row is moved) */
static int ab_scaler_sel;
static const char *men_ab_scaler[] = { "1x1", "Integer 2x", "4:3", "Integer 4:3", "Fullscreen", NULL };
static const char h_ab_scaler[]  = "1x1: the PlayStation's pixels as they are. Integer: whole multiples"
                                   " only (sharpest). 4:3: as the PlayStation drew it. Fullscreen: the whole"
                                   " screen";
static const char *men_ab_scanlines[] = { "Off", "1", "2", "3", NULL };
static const char h_ab_scanline_l[] = "How much of the picture shows through the dark lines, 0-100%";
static const char h_ab_pad[]     = "Standard (digital), analog (DualShock), a gun or nothing;"
                                   " takes effect when the game goes on";
/* the Exit row's help, on every platform (ab_buttons.h) */
static const char h_ab_exit[]    = "Back to AutoBleem - holding the menu button for 2 seconds in the game"
                                   " does the same";
/* ab_buttons.c autosaves the game into RAM every 30 s of play (state_sony.c's SaveStateAuto) */
static const char h_ab_autoload[] = "The game as it was up to 30 seconds ago: it is saved in memory by itself"
                                    " while you play";
static const char h_ab_reset[] = "Starts the game over from the beginning, as the console's Reset did";
/* the output mode (ab_config.h): the modes the display lists, filled when the menu opens */
static int ab_display_sel, ab_display_modes[AB_OUTPUT_MAX_MODES];
static const char *men_ab_display[AB_OUTPUT_MAX_MODES + 1];
static char ab_display_names[AB_OUTPUT_MAX_MODES][16];
static const char h_ab_display[] = "The resolution the screen is driven at; only what the TV or monitor offers"
                                   " is listed";
/* gpu_neon's enhancement (the built-in GPU only) and the seams fix in it (psx_gpu_parse.c) */
static const char *men_ab_enhance[] = { "1x", "2x", NULL };
static const char h_ab_enhance[] = "2x draws the PlayStation's 3D at double resolution (not in high resolution"
                                   " games); costs speed";
static const char h_ab_noseams[] = "No 1-pixel gaps between pictures made of several parts at 2x";
/* the GPU's dithering, upstream's pl_rearmed_cbs.dithering (pcsx.cfg "dithering2": 0 off, 1 where the game
 * asks for it, 2 on everything) - an unsigned char there, so the row edits a copy (ab_menu_loop_d) */
static int ab_dither_sel;
static const char *men_ab_dither[] = { "Off", "On", "Always", NULL };
static const char h_ab_dither[] = "The PlayStation's fine dot pattern that hides colour steps. On: where the"
                                  " game uses it. Always: on everything. Off: flat colours";
/* why a row is greyed */
static const char ab_why_crt[]   = "Off while a CRT filter is on";
static const char ab_why_1x[]    = "Only with the 2x resolution";
static const char ab_why_display[] = "The console's resolution is chosen in AutoBleem's settings";

/* the sections are label rows (not selectable), drawn as headings */
static menu_entry e_menu_ab[] =
{
	mee_label     ("Game"),
	mee_handler_id("Resume game",              MA_MAIN_RESUME_GAME, main_menu_handler),
	mee_label     ("Saves"),
	mee_handler_id("Quick save",               MA_AB_QUICKSAVE,     ab_menu_handler),
	mee_handler_id("Quick load",               MA_AB_QUICKLOAD,     ab_menu_handler),
	mee_handler_id_h("Load autosave",          MA_AB_AUTOLOAD,      ab_menu_handler, h_ab_autoload),
	mee_label     ("CD disc"),
	mee_handler_id("Change disc",              MA_AB_DISC,          ab_menu_handler),
	mee_handler_id_h("Reset game",             MA_MAIN_RESET_GAME,  main_menu_handler, h_ab_reset),
	mee_label     ("Picture"),
	mee_enum_h    ("Display",                  MA_AB_DISPLAY,       ab_display_sel, men_ab_display, h_ab_display),
	mee_enum_h    ("Resolution",               MA_AB_ENHANCE,       pl_rearmed_cbs.gpu_neon.enhancement_enable, men_ab_enhance, h_ab_enhance),
	mee_onoff_h   ("Remove seams",             MA_AB_NOSEAMS,       pl_rearmed_cbs.gpu_neon.enhancement_no_seams, 1, h_ab_noseams),
	mee_enum_h    ("Dithering",                MA_AB_DITHER,        ab_dither_sel, men_ab_dither, h_ab_dither),
	mee_enum_h    ("Scaling",                MA_AB_SCALER,        ab_scaler_sel, men_ab_scaler, h_ab_scaler),
	mee_enum_h    ("Smoothing",                MA_OPT_SWFILTER,     soft_filter, men_ab_smooth, h_ab_smooth),
	mee_cust_h    ("Filter",                   MA_AB_FILTER,        ab_menu_handler, ab_filter_name, h_ab_filter),
	mee_enum_h    ("Scanlines",                MA_OPT_SCANLINES,    scanlines, men_ab_scanlines, h_ab_scanlines),
	mee_range_h   ("Scanline brightness",      MA_OPT_SCANLINE_LEVEL, scanline_level, 0, 100, h_ab_scanline_l),
	mee_label     ("Controllers"),
	mee_enum_h    ("Controller 1",             0,                   in_type_sel1, men_in_type_sel, h_ab_pad),
	mee_enum_h    ("Controller 2",             0,                   in_type_sel2, men_in_type_sel, h_ab_pad),
	mee_label     ("Settings"),
	mee_handler_id_h("Save settings for this game", MA_AB_SAVECFG,  ab_menu_handler, h_ab_savecfg),
	mee_handler_id_h("PCSX menu",              MA_AB_PCSX_MENU,     ab_menu_pcsx_handler, h_ab_pcsx),
	mee_label     ("Leave"),
	mee_handler_id_h("Exit",                   MA_MAIN_EXIT,        main_menu_handler, NULL),
	mee_end,
};

/* the pages open above the one on screen (the rows that opened them), for the path over the panel; and
 * the info rows' texts of this frame (ab_menu_draw) */
#define AB_MENU_MAX_DEPTH 6
#define AB_MENU_MAX_ROWS  64
static const char *ab_crumbs[AB_MENU_MAX_DEPTH];
static int ab_crumb_n;
static const char *ab_gen_names[AB_MENU_MAX_ROWS];
/* the scroll of the page on screen (a list longer than the panel), in pixels of the list */
static const menu_entry *ab_scroll_menu;
static int ab_scroll;

/* The top menu's tabs, L1/R1 (the owner, 2026-09-29): the Picture section is the second tab, Controllers
 * the third, the rest the first. A tab's own heading is not drawn (the tab bar says it); each tab keeps its
 * selected row, and the menu opens again on the tab and row it was left on (for the run). The pages under
 * the top menu (the PCSX menu, the cheats) have no tabs. */
#define AB_TABS 3
static const char *ab_tab_names[AB_TABS] = { "Game", "Picture", "Controllers" };
static int ab_tab, ab_tab_sel[AB_TABS] = { -1, -1, -1 };

static int ab_row_tab(const menu_entry *menu, int i)
{
	const char *section = "";
	int k;
	for (k = 0; k <= i && menu[k].name; k++)
		if (!menu[k].selectable && menu[k].generate_name == NULL && menu[k].name[0] != 0)
			section = menu[k].name;
	for (k = 1; k < AB_TABS; k++)
		if (strcmp(section, ab_tab_names[k]) == 0)
			return k;
	return 0;
}

/* whether row i belongs on the page as it is now: always, but in the top menu only the current tab's rows,
 * without the tab's own heading */
static int ab_in_tab(const menu_entry *menu, int i)
{
	if (menu != e_menu_ab)
		return 1;
	if (!menu[i].selectable && menu[i].generate_name == NULL && strcmp(menu[i].name, ab_tab_names[ab_tab]) == 0)
		return 0;
	return ab_row_tab(menu, i) == ab_tab;
}

/* a row that takes space on the panel: enabled, not upstream's empty spacer label, and an info row only
 * with a text this frame (ab_menu_draw's first pass fills ab_gen_names) */
static int ab_row_shown(const menu_entry *ent, int i)
{
	if (!ent->enabled)
		return 0;
	if (ent->selectable)
		return 1;
	if (ent->generate_name != NULL)
		return i < AB_MENU_MAX_ROWS && ab_gen_names[i] != NULL;
	return ent->name[0] != 0;
}

/* the reason a row is greyed now (it stays selectable, its help says why, its value does not move), or
 * NULL: a CRT filter draws its own scanlines, and on the console it rules out the smoothing too */
static const char *ab_row_blocked(const menu_entry *e)
{
	if (e->id == MA_AB_NOSEAMS && !pl_rearmed_cbs.gpu_neon.enhancement_enable)
		return ab_why_1x;
	if (e->id == MA_AB_DISPLAY && ab_console_present())
		return ab_why_display;
	if (!ab_filter_is_crt(plat_target.hwfilter))
		return NULL;
	if (e->id == MA_OPT_SCANLINES || e->id == MA_OPT_SCANLINE_LEVEL)
		return ab_why_crt;
	if (e->id == MA_OPT_SWFILTER && ab_console_present())
		return ab_why_crt;
	return NULL;
}

/* the game's last frame before the menu opened, kept in RAM for the menu (nothing written anywhere): the
 * clean frame as the console drew it, taken every time the menu opens, so it is never empty */
static unsigned short *ab_snap;
static int ab_snap_w, ab_snap_h;

static void ab_snap_take(void)
{
	int w, h, bpp, y;
	void *src = pl_prepare_screenshot(&w, &h, &bpp);
	unsigned short *n;

	if (src == NULL || bpp != 16 || w <= 0 || h <= 0)
		return;
	n = realloc(ab_snap, (size_t)w * h * 2);
	if (n == NULL)
		return;
	for (y = 0; y < h; y++)
		memcpy(n + (size_t)y * w, (unsigned short *)src + (size_t)y * w, (size_t)w * 2);
	ab_snap = n;
	ab_snap_w = w;
	ab_snap_h = h;
}

/* The rows that edit a copy of their setting: Scaling is g_scaler without "custom" (a custom layer shows as
 * 4:3 and is left alone unless the row was moved), Dithering an unsigned char. The copies are taken when
 * the menu shows and written back only when a row was moved - the PCSX menu beneath may have changed the
 * setting itself meanwhile - before a save and when the menu closes. */
static int ab_scaler_shown, ab_dither_shown;

static void ab_rows_take(void)
{
	ab_scaler_sel = ab_scaler_shown = g_scaler <= SCALE_FULLSCREEN ? g_scaler : SCALE_4_3;
	ab_dither_sel = ab_dither_shown = pl_rearmed_cbs.dithering <= 2 ? pl_rearmed_cbs.dithering : 1;
}

static void ab_rows_commit(void)
{
	if (ab_scaler_sel != ab_scaler_shown)
		g_scaler = ab_scaler_shown = ab_scaler_sel;
	/* the GPU takes it when the game goes on (menu_prepare_emu -> plugin_call_rearmed_cbs) */
	if (ab_dither_sel != ab_dither_shown)
		pl_rearmed_cbs.dithering = ab_dither_shown = ab_dither_sel;
}

/* the game's own config, pcsx.custom.cfg - what every save in these menus writes (ab_config.h) */
static int ab_save_config(void)
{
	ab_rows_commit();
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
	case MA_AB_AUTOLOAD:
		if (!ready_to_go || !CdromId[0])
			break;
		if (StateAutoAge() < 0) {
			menu_update_msg("No autosave yet");
			break;
		}
		ret = LoadStateAuto();
		menu_update_msg(ret == 0 ? "Autosave loaded" : "Autosave load failed");
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
		if (ab_filter_is_crt(plat_target.hwfilter))
			menu_update_msg(ab_console_present() && soft_filter != SOFT_FILTER_NONE ?
				"A CRT filter draws its own scanlines; smoothing is off with it" :
				"A CRT filter draws its own scanlines");
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

/* the look is data - skin/skin.cfg through ab_ui_skin() (docs/skin.md), ab2.0.0's values built in */
#define ab_col_text       (ab_ui_skin()->text)
#define ab_col_dim        (ab_ui_skin()->dim)		/* values, help */
#define ab_col_accent     (ab_ui_skin()->accent)	/* rims, headings, arrows */
#define ab_col_panel      (ab_ui_skin()->panel)
#define ab_col_row        (ab_ui_skin()->row)		/* the selected row's wash, the L1/R1 chips */
#define ab_col_select_rim (ab_ui_skin()->select_rim)	/* the selected row's rim */
#define ab_col_name       (ab_ui_skin()->name)
#define ab_col_shadow     (ab_ui_skin()->shadow)
#define ab_col_grey       (ab_ui_skin()->grey)		/* a greyed row */
#define AB_PANEL_ALPHA    (ab_ui_skin()->panel_alpha)	/* every panel over the art */
#define AB_ROW_ALPHA      (ab_ui_skin()->row_alpha)	/* the selected row's wash over its panel */

/* the layout, in the 1280x720 design (autobleem-design themes/ab2.0.0/design/emu/README.md, the mockup
 * emu-menu-v2-p5.png): the rows' panel from x 32, 540 wide, between y AB_PANEL_TOP and AB_PANEL_BOTTOM -
 * 18 above the art's hint bar (614) and clear of its logo */
#define AB_PANEL_TOP      24
#define AB_PANEL_BOTTOM   596
#define AB_PANEL_CUT      16	/* ab2.0.0's cut corners (top right, bottom left): the panels' */
#define AB_ROW_CUT        8	/* the selected row's */
#define AB_RIM            2	/* the rims' thickness */
#define AB_ROW_INSET      16	/* the selected row's shape from the panel's sides */
#define AB_NAME_X         40	/* the rows' names from the panel's left (headings 12 less) */
#define AB_VALUE_X        32	/* the values' right edge from the panel's right */
#define AB_RULE_END       24	/* a heading's rule ends this far from the panel's right */
/* the art's hint bar (466..1268 x 614..684) carries only the hints: from x 490, on the bar's middle line;
 * the build's two lines sit right-aligned just above it, under the right-hand column */
#define AB_HINT_X         490
#define AB_HINT_CY        649
#define AB_HINT_R         14	/* the pad glyph's radius (the launcher's 30 px glyphs) */
#define AB_HINT_GAP       48	/* from a hint's text to the next glyph */
#define AB_BUILD_X        1248	/* the build lines' right edge (the bar's inner right) */
#define AB_BUILD_Y        568	/* the first build line's top; the second 22 below */

static unsigned short *ab_bg;		/* the art at the canvas' size, bright (ab_menu_prepare_bg) */
static int ab_bg_w, ab_bg_h;

static struct ab_canvas ab_canvas(void)
{
	struct ab_canvas c = { g_menuscreen_ptr, g_menuscreen_w, g_menuscreen_h, g_menuscreen_pp };
	return c;
}

/* a rim's thickness at the canvas' scale, at least a pixel */
static int ab_rim(float s)
{
	int t = (int)(AB_RIM * s + 0.5f);
	return t < 1 ? 1 : t;
}

/* a panel in the ab2.0.0 style: the cut corners, the accent rim, the skin's panel colour and alpha */
static void ab_panel(struct ab_canvas *c, int x, int y, int w, int h, float s)
{
	ab_ui_cut_panel(c, x, y, w, h, (int)(AB_PANEL_CUT * s), ab_rim(s), ab_col_accent, ab_col_panel, AB_PANEL_ALPHA);
}

/* a small filled triangle, pointing up or down: more rows above or below the panel's view */
static void ab_chevron(struct ab_canvas *c, int cx, int y, int size, int up)
{
	int k;
	for (k = 0; k < size; k++) {
		int w = up ? k : size - 1 - k;
		ab_ui_fill(c, cx - w, y + k, 2 * w + 1, 1, 0, ab_col_accent, 230);
	}
}

/* the design's text sizes are line boxes of Selawik's proportions; the skin's text_scale draws another
 * font's glyphs as large (Red Hat Text: 110 %) - the box's top stays where the layout puts it, and its
 * baseline lands within a pixel of where Selawik's was */
static int ab_px(int px)
{
	return (px * ab_ui_skin()->text_scale + 50) / 100;
}

static int ab_text_width(const char *s, int px)
{
	return ab_ui_has_font() ? ab_ui_text_width(s, ab_px(px)) : (int)strlen(s) * me_mfont_w;
}

/* returns the text's width */
static int ab_text(struct ab_canvas *c, int x, int y, int align, const char *s, int px, unsigned short col)
{
	int w;

	if (ab_ui_has_font())
		return ab_ui_text(c, x, y, align, s, ab_px(px), col);
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

/* the same, returning the text's width */
static int ab_text_shadow_w(struct ab_canvas *c, int x, int y, const char *s, int px, unsigned short col)
{
	int d = px >= 30 ? 2 : 1;
	ab_text(c, x + d, y + d, AB_UI_LEFT, s, px, ab_col_shadow);
	return ab_text(c, x, y, AB_UI_LEFT, s, px, col);
}

/* a small battery (the HUD's icon's shape) filled to `percent`, top left at (x, y); returns its width */
static int ab_battery_icon(struct ab_canvas *c, int x, int y, float s, int percent)
{
	int w = (int)(30 * s), h = (int)(15 * s), t = s >= 1.5f ? 2 : 1, nub = (int)(3 * s) + 1, fw;
	unsigned short fill = percent <= AB_PAD_BATTERY_LOW_PERCENT ? AB_RGB565(0xff, 0x40, 0x30) :
			      percent <= 50 ? AB_RGB565(0xff, 0xa0, 0x00) : AB_RGB565(0x40, 0xd8, 0x60);

	ab_ui_fill(c, x - 1, y - 1, w + nub + 2, h + 2, 0, ab_col_shadow, 200);
	ab_ui_fill(c, x, y, w, t, 0, ab_col_text, 255);
	ab_ui_fill(c, x, y + h - t, w, t, 0, ab_col_text, 255);
	ab_ui_fill(c, x, y, t, h, 0, ab_col_text, 255);
	ab_ui_fill(c, x + w - t, y, t, h, 0, ab_col_text, 255);
	ab_ui_fill(c, x + w, y + h / 3, nub, h - 2 * (h / 3), 0, ab_col_text, 255);
	fw = (w - 4 * t) * percent / 100;
	if (fw > 0)
		ab_ui_fill(c, x + 2 * t, y + 2 * t, fw, h - 4 * t, 0, fill, 255);
	return w + nub;
}

/* a pad glyph (the launcher's hint bar's: a disc with a blue Cross or a red Circle) and its text, centred
 * on cy, left to right from *x, which moves past them */
static void ab_hint(struct ab_canvas *c, int *x, int cy, int px, int is_cross, const char *text)
{
	float s = c->h / 720.0f;
	int r = (int)(AB_HINT_R * s), gap = (int)(10 * s);

	ab_ui_pad_glyph(c, *x + r, cy, r, is_cross, ab_ui_skin()->hint_disc, ab_ui_skin()->hint_rim,
			is_cross ? ab_ui_skin()->hint_cross : ab_ui_skin()->hint_circle);
	*x += 2 * r + gap;
	*x += ab_text(c, *x, cy - px / 2, AB_UI_LEFT, text, px, ab_col_text) + (int)(AB_HINT_GAP * s);
}

/* "(x) ok   (o) back" on the art's bar, from its left end; back == NULL for a plain "(x) OK" */
static void ab_footer(struct ab_canvas *c, const char *ok, const char *back)
{
	float s = c->h / 720.0f;
	int px = (int)(22 * s), x = (int)(AB_HINT_X * s), cy = (int)(AB_HINT_CY * s);

	ab_hint(c, &x, cy, px, 1, ok);
	if (back != NULL)
		ab_hint(c, &x, cy, px, 0, back);
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
	ab_panel(&c, x0 - r - (int)(60 * s), cy - r - (int)(50 * s), (n - 1) * step + 2 * r + (int)(120 * s),
		 2 * r + (int)(130 * s), s);
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

/* the message box; with ok == NULL no hint (the way out's "Please wait...", which takes no key) */
static void ab_draw_box(const char *msg, const char *ok)
{
	struct ab_canvas c;
	float s;

	int px, w;

	c = ab_screen_begin();
	s = c.h / 720.0f;
	px = (int)(32 * s);
	w = ab_text_width(msg, px) + (int)(120 * s);
	ab_panel(&c, (c.w - w) / 2, (int)(c.h * 0.44f) - (int)(40 * s), w, px + (int)(80 * s), s);
	ab_text(&c, c.w / 2, (int)(c.h * 0.44f), AB_UI_CENTER, msg, px, ab_col_text);
	if (ok != NULL)
		ab_footer(&c, ok, NULL);
	menu_draw_end();
}

static void ab_draw_message(const char *msg)
{
	ab_debug_screen("message");
	ab_draw_box(msg, ab_ui_str(AB_STR_OK));
}

/* The way out's "Please wait..." (ab_session_exit(), before the resume point and its files are written,
 * which on a console's CPU and stick takes a moment): the message box over the menu's art, no hint. Safe
 * for the resume picture: it is the emulated frame (plat_prepare_screenshot()'s shadow_fb), not the screen,
 * and menu_leave_emu() only copies it - so the box can go up as soon as the run has ended */
void ab_menu_wait_screen(void)
{
	menu_leave_emu();
	ab_menu_prepare_bg();
	ab_ui_load(ab_opts.language);
	ab_draw_box(ab_ui_tr("Please wait..."), NULL);
	ab_debug_screen("wait");
}

/* the buttons that got us here are not the screen's: wait only for those still held - in_menu_wait_any()
 * reads a key, so asking it when nothing is held ate the first press made as the screen came up (BUG-52:
 * the menu opened from the game, whose keys never reach the menus' key state, lost its first Down/Return) */
static void ab_wait_released(void)
{
	int held = in_menu_keys_held() & (PBTN_MOK|PBTN_MBACK|PBTN_MENU);

	while (held && (held &= in_menu_wait_any(NULL, 50))) {
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
 * behind everything (its logo bottom left, its bar along the bottom), the rows on a translucent panel on
 * the left, the game's name and id top right with its picture and the selected row's help (or the last
 * message) under them, the pad hints and the build on the bar. Without the art (no skin/) the same over plain navy. */

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

/* how many of ab_text_wrap's lines of px, from y, end above `bottom` - at most `max` */
static int ab_lines_fit(int y, int bottom, int px, int max)
{
	int n = 0, glyph_h = ab_ui_has_font() ? ab_px(px) : px;

	while (n < max && y + n * (px + px / 4) + glyph_h <= bottom)
		n++;
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
	int heads, head_h, head_px, avail, left_w, total, view_h, help_bottom;
	const char *blocked;

	ab_debug_screen("menu");
	c = ab_screen_begin();
	s = c.h / 720.0f;

	/* the rows' panel on the left, the game's name, batteries, picture and help right of it */
	panel_w = (int)(540 * s);
	panel_x = (int)(32 * s);
	left_w = c.w - (int)(40 * s) - (panel_x + panel_w + (int)(40 * s));

	/* top right: the game */
	x = panel_x + panel_w + (int)(40 * s);
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

	/* the rows, on the panel: sections (the label rows) as headings, between the top and the art's bar (a
	 * 1280x720 design); a list taller than that scrolls with the selection */
	for (n = 0, heads = 0, ent = menu, i = 0; ent->name; ent++, i++) {
		if (i < AB_MENU_MAX_ROWS)
			ab_gen_names[i] = NULL;
		if (!ent->enabled || !ab_in_tab(menu, i))
			continue;
		if (i == sel)
			ent_sel = ent;
		if (ent->selectable)
			n++;
		else if (ent->generate_name != NULL) {
			/* an info row (upstream's device list): its text once a frame, in order - the generators
			 * iterate (MA_CTRL_DEV_FIRST starts over); an empty one is not shown */
			int offs = 0;
			const char *t = ent->generate_name(ent->id, &offs);
			if (i < AB_MENU_MAX_ROWS && t != NULL && t[0] != 0) {
				ab_gen_names[i] = t;
				n++;
			}
		} else if (ent->name[0] != 0)
			heads++;	/* an empty label is upstream's spacer: not shown */
	}
	if (ent_sel != NULL)
		menu_sel_name = ent_sel->name;	/* as libpicofe's menus do, for the debug driver */
	/* sizes read from a sofa (the owner, 2026-09-28): a list longer than the panel scrolls instead of
	 * shrinking */
	row_h = (int)(38 * s);
	head_h = (int)(34 * s);
	pad = (int)(16 * s);
	avail = (int)((AB_PANEL_BOTTOM - AB_PANEL_TOP) * s);
	panel_y = (int)(AB_PANEL_TOP * s);
	if (ab_crumb_n > 0) {
		/* a page one level down or more: where it is, over the panel ("PCSX menu > Options > Display") */
		int k, cx = panel_x + (int)(8 * s);
		for (k = 0; k < ab_crumb_n; k++) {
			char cb[96];
			const char *t = ab_crumbs[k];
			if (t[0] == '[' && strlen(t) > 2) {
				snprintf(cb, sizeof(cb), "%.*s", (int)strlen(t) - 2, t + 1);
				t = cb;
			}
			if (k > 0)
				cx += ab_text_shadow_w(&c, cx, panel_y, ">", (int)(22 * s), ab_col_dim) + (int)(10 * s);
			cx += ab_text_shadow_w(&c, cx, panel_y, ab_ui_tr(t), (int)(22 * s),
				k == ab_crumb_n - 1 ? ab_col_accent : ab_col_dim) + (int)(10 * s);
		}
		panel_y += (int)(36 * s);
		avail -= (int)(36 * s);
	} else if (menu == e_menu_ab) {
		/* the tab bar: [L1]  Game  Picture  [R1] - the current tab bright and underlined, the shoulder
		 * buttons as chips at both ends so switching is obvious */
		int k, tx = panel_x + (int)(8 * s), ty = panel_y, chip_px = (int)(18 * s), tab_px = (int)(24 * s);
		int chip_h = (int)(30 * s), chip_w = (int)(44 * s), gap = (int)(22 * s);
		ab_ui_cut_panel(&c, tx, ty, chip_w, chip_h, (int)(AB_ROW_CUT * s), ab_rim(s), ab_col_select_rim, ab_col_row, AB_ROW_ALPHA);
		ab_text(&c, tx + chip_w / 2, ty + (chip_h - chip_px) / 2, AB_UI_CENTER, "L1", chip_px, ab_col_text);
		tx += chip_w + gap;
		for (k = 0; k < AB_TABS; k++) {
			int cur = k == ab_tab, tw;
			tw = ab_text_shadow_w(&c, tx, ty + (chip_h - tab_px) / 2 - (int)(1 * s), ab_ui_tr(ab_tab_names[k]),
				tab_px, cur ? ab_col_accent : ab_col_dim);
			if (cur)
				ab_ui_fill(&c, tx, ty + chip_h + (int)(3 * s), tw, (int)(3 * s), 0, ab_col_accent, 255);
			tx += tw + gap;
		}
		ab_ui_cut_panel(&c, tx, ty, chip_w, chip_h, (int)(AB_ROW_CUT * s), ab_rim(s), ab_col_select_rim, ab_col_row, AB_ROW_ALPHA);
		ab_text(&c, tx + chip_w / 2, ty + (chip_h - chip_px) / 2, AB_UI_CENTER, "R1", chip_px, ab_col_text);
		panel_y += (int)(46 * s);
		avail -= (int)(46 * s);
	}
	px = row_h * 7 / 10;
	head_px = head_h * 6 / 10;
	total = n * row_h + heads * head_h;
	view_h = avail - 2 * pad;
	{
		/* where the selected row is in the list (with its section's heading when it is the section's
		 * first row), and the scroll that keeps it in view with a row of margin */
		int vy = 0, head_top = -1, sel_top = 0, sel_bot = 0, prev_head = 0, h;
		for (ent = menu, i = 0; ent->name; ent++, i++) {
			if (!ab_row_shown(ent, i) || !ab_in_tab(menu, i))
				continue;
			h = ent->selectable || ent->generate_name != NULL ? row_h : head_h;
			if (i == sel) {
				sel_top = prev_head ? head_top : vy;
				sel_bot = vy + h;
			}
			prev_head = !ent->selectable && ent->generate_name == NULL;
			if (prev_head)
				head_top = vy;
			vy += h;
		}
		if (menu != ab_scroll_menu) {
			ab_scroll_menu = menu;
			ab_scroll = 0;
		}
		if (total <= view_h) {
			ab_scroll = 0;
		} else {
			if (sel_top - (sel_top > 0 ? row_h / 2 : 0) < ab_scroll)
				ab_scroll = sel_top - (sel_top > 0 ? row_h / 2 : 0);
			if (sel_bot + (sel_bot < total ? row_h : 0) > ab_scroll + view_h)
				ab_scroll = sel_bot + (sel_bot < total ? row_h : 0) - view_h;
			if (ab_scroll > total - view_h)
				ab_scroll = total - view_h;
			if (ab_scroll < 0)
				ab_scroll = 0;
		}
	}
	panel_h = (total < view_h ? total : view_h) + 2 * pad;
	ab_panel(&c, panel_x, panel_y, panel_w, panel_h, s);
	if (ab_scroll > 0)
		ab_chevron(&c, panel_x + panel_w / 2, panel_y + pad / 2 - (int)(3 * s), (int)(7 * s), 1);
	if (ab_scroll + view_h < total)
		ab_chevron(&c, panel_x + panel_w / 2, panel_y + panel_h - pad / 2 - (int)(4 * s), (int)(7 * s), 0);
	x_name = panel_x + (int)(AB_NAME_X * s);
	x_val = panel_x + panel_w - (int)(AB_VALUE_X * s);
	y = panel_y + pad - ab_scroll;
	for (ent = menu, i = 0; ent->name; ent++, i++) {
		const char *name = ent->name, *val = NULL;
		char namebuf[96];
		int offs = 0, is_sel = i == sel, page, h;
		unsigned short col_name, col_val;

		if (!ab_row_shown(ent, i) || !ab_in_tab(menu, i))
			continue;
		/* only what is wholly inside the panel's view */
		h = ent->selectable || ent->generate_name != NULL ? row_h : head_h;
		if (y < panel_y + pad - 1 || y + h > panel_y + pad + view_h + 1) {
			y += h;
			continue;
		}
		if (!ent->selectable && ent->generate_name != NULL) {
			ab_text(&c, x_name, y + (row_h - px) / 2, AB_UI_LEFT, ab_gen_names[i], px, ab_col_dim);
			y += row_h;
			continue;
		}
		if (!ent->selectable) {
			/* a section's heading, with a thin rule after it to the panel's right */
			int tw, rx;
			tw = ab_text(&c, x_name - (int)(12 * s), y + head_h - head_px - (int)(4 * s), AB_UI_LEFT,
				ab_ui_tr(name), head_px, ab_col_accent);
			rx = x_name - (int)(12 * s) + tw + (int)(10 * s);
			ab_ui_fill(&c, rx, y + head_h - head_px / 2 - (int)(4 * s),
				panel_x + panel_w - (int)(AB_RULE_END * s) - rx, s >= 2 ? (int)s : 1, 0, ab_col_accent, 255);
			y += head_h;
			continue;
		}
		blocked = ab_row_blocked(ent);
		col_name = blocked ? ab_col_grey : is_sel ? ab_col_text : ab_col_name;
		col_val = blocked ? col_name : ab_col_dim;
		if (is_sel)
			ab_ui_cut_panel(&c, panel_x + (int)(AB_ROW_INSET * s), y, panel_w - 2 * (int)(AB_ROW_INSET * s), row_h,
				(int)(AB_ROW_CUT * s), ab_rim(s), ab_col_select_rim, ab_col_row, AB_ROW_ALPHA);
		if (name[0] == 0 && ent->generate_name != NULL)
			name = ent->generate_name(ent->id, &offs);
		/* "[name]" opens a page: the name without its brackets and a ">" where a value would be */
		page = name[0] == '[' && strlen(name) > 2 && name[strlen(name) - 1] == ']';
		if (page) {
			snprintf(namebuf, sizeof(namebuf), "%.*s", (int)strlen(name) - 2, name + 1);
			name = namebuf;
		}
		name = ab_ui_tr(name);
		if (page)
			ab_text(&c, x_val, y + (row_h - px) / 2, AB_UI_RIGHT, ">", px, is_sel ? ab_col_accent : ab_col_dim);
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
		if (val != NULL)
			val = ab_ui_tr(val);
		{
			/* rows are one line: a name that would run into its value (or the page's ">") is drawn
			 * smaller, down to 70 % - upstream's long PCSX-menu names in the longer languages; the
			 * arrows of a selected value row are counted on every row, so a name keeps its size */
			int room = x_val - x_name - (int)(16 * s), npx = px;
			if (val != NULL)
				room -= ab_text_width(val, px) +
					(!blocked ? 2 * (ab_text_width("<", px) + (int)(8 * s)) : 0);
			else if (page)
				room -= ab_text_width(">", px);
			while (npx > px * 7 / 10 && ab_text_width(name, npx) > room)
				npx--;
			ab_text(&c, x_name, y + (row_h - npx) / 2, AB_UI_LEFT, name, npx, col_name);
		}
		if (val != NULL) {
			int vx = x_val, vy = y + (row_h - px) / 2;
			if (blocked) {
				ab_text(&c, vx, vy, AB_UI_RIGHT, val, px, col_val);
			} else if (is_sel) {
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

	/* right of the panel, under the game's name: the pads' batteries, the game's last frame, then the
	 * message of the moment or the selected row's help (left_w is that column's width) */
	x = panel_x + panel_w + (int)(40 * s);
	y = (int)(40 * s) + (CdromId[0] != 0 ? (int)(84 * s) : (int)(50 * s));
	{
		int pct[AB_PAD_BATTERY_MAX], np = ab_pad_battery_all(pct, AB_PAD_BATTERY_MAX), k, bx;
		if (np > 0) {
			bx = x + ab_text_shadow_w(&c, x, y, ab_ui_tr("Pad battery"), (int)(20 * s), ab_col_dim) + (int)(12 * s);
			for (k = 0; k < np; k++) {
				snprintf(buf, sizeof(buf), "%d%%", pct[k]);
				bx += ab_battery_icon(&c, bx, y + (int)(3 * s), s, pct[k]) + (int)(6 * s);
				bx += ab_text_shadow_w(&c, bx, y, buf, (int)(20 * s),
					pct[k] <= AB_PAD_BATTERY_LOW_PERCENT ? AB_RGB565(0xff, 0x60, 0x50) : ab_col_text) + (int)(18 * s);
			}
			y += (int)(34 * s);
		}
	}
	{
		/* the game's last frame, 4:3 in a frame: a panel of the cut-corner shape, the picture inside it cut
		 * the same way, fr from the frame across its diagonal too (a cut (2 - sqrt(2)) * fr less) */
		int tw = left_w < (int)(400 * s) ? left_w : (int)(400 * s), th = tw * 3 / 4, fr = (int)(6 * s);
		int cut = (int)(AB_PANEL_CUT * s), icut = cut - (fr * 586 + 500) / 1000;
		ab_panel(&c, x - fr, y - fr, tw + 2 * fr, th + 2 * fr, s);
		if (ab_snap != NULL) {
			int tx, ty;
			for (ty = 0; ty < th; ty++) {
				const unsigned short *srow = ab_snap + (size_t)(ty * ab_snap_h / th) * ab_snap_w;
				unsigned short *drow = c.fb + (size_t)(y + ty) * c.pitch + x;
				int l = icut - (th - 1 - ty), r = icut - ty;
				if (y + ty < 0 || y + ty >= c.h)
					continue;
				for (tx = l > 0 ? l : 0; tx < tw - (r > 0 ? r : 0); tx++)
					drow[tx] = srow[tx * ab_snap_w / tw];
			}
		}
		y += th + fr + (int)(24 * s);
	}
	blocked = ent_sel != NULL ? ab_row_blocked(ent_sel) : NULL;
	/* the column ends above the build lines */
	help_bottom = (int)((AB_BUILD_Y - 8) * s);
	if (menu_error_msg[0] != 0) {
		ab_text_wrap(&c, x, y, ab_ui_tr(menu_error_msg), (int)(22 * s), left_w,
			     ab_lines_fit(y, help_bottom, (int)(22 * s), 3), ab_col_accent);
		if (plat_get_ticks_ms() - menu_error_time > 2048)
			menu_error_msg[0] = 0;
	} else if (blocked != NULL) {
		ab_text_wrap(&c, x, y, ab_ui_tr(blocked), (int)(20 * s), left_w,
			     ab_lines_fit(y, help_bottom, (int)(20 * s), 3), ab_col_accent);
	} else if (ent_sel != NULL && ent_sel->help != NULL) {
		/* upstream's help texts break their lines with '\n' for its 8x8 font: one paragraph here, wrapped */
		/* (spaces collapsed too: the language files' keys are these texts in one line) */
		char help[640], *p, *q;
		snprintf(help, sizeof(help), "%s", ent_sel->help);
		for (p = q = help; *p; p++) {
			char ch = *p == '\n' ? ' ' : *p;
			if (ch == ' ' && (q == help || q[-1] == ' '))
				continue;
			*q++ = ch;
		}
		while (q > help && q[-1] == ' ')
			q--;
		*q = 0;
		ab_text_wrap(&c, x, y, ab_ui_tr(help), (int)(20 * s), left_w,
			     ab_lines_fit(y, help_bottom, (int)(20 * s), 5), ab_col_dim);
	}

	/* the bar: only the hints; the build right-aligned just above it */
	ab_footer(&c, ab_ui_tr("Select"), ab_ui_tr(ab_crumb_n > 0 || !ready_to_go ? "Back" : "Resume"));
	px = (int)(17 * s);
	x = (int)(AB_BUILD_X * s);
	/* the package's version (AB_VERSION, exported by the launcher) - what every program on the stick shows;
	   the emulator's own git describe only when it was started without the launcher */
	{
		const char *version = getenv("AB_VERSION");
		if (version && *version)
			snprintf(buf, sizeof(buf), "AutoBleem %s", version);
		else
			snprintf(buf, sizeof(buf), "pcsx-abnxt %s", REV[0] != 0 ? REV : "(no version)");
	}
	ab_text_shadow(&c, x, (int)(AB_BUILD_Y * s), AB_UI_RIGHT, buf, px, ab_col_dim);
	snprintf(buf, sizeof(buf), "PCSX-ReARMed  \xc2\xb7  %s  \xc2\xb7  %s  \xc2\xb7  built %s", ab_cpu_name(), ab_gpu_name(), __DATE__);
	ab_text_shadow(&c, x, (int)((AB_BUILD_Y + 22) * s), AB_UI_RIGHT, buf, px, ab_col_dim);

	menu_draw_end();
}

/* The PCSX menu's About (upstream's Credits): our name, the version, the year, then upstream's credits,
 * on a panel in the middle of the menu's art */
static void ab_about_screen(const char *upstream_credits)
{
	struct ab_canvas c;
	const char *version = getenv("AB_VERSION"), *p;
	char buf[128];
	float s;
	int pw, ph, px0, py0, cx, y, inp;

	ab_wait_released();
	for (;;) {
		ab_debug_screen("about");
		c = ab_screen_begin();
		s = c.h / 720.0f;
		pw = (int)(760 * s);
		ph = (int)(560 * s);
		px0 = (c.w - pw) / 2;
		py0 = (int)(40 * s);
		cx = c.w / 2;
		ab_panel(&c, px0, py0, pw, ph, s);

		y = py0 + (int)(34 * s);
		ab_text(&c, cx, y, AB_UI_CENTER, "PCSX-AutoBleem Next", (int)(46 * s), ab_col_text);
		y += (int)(64 * s);
		if (version && *version)
			snprintf(buf, sizeof(buf), "%s %s", ab_ui_tr("Version"), version);
		else
			snprintf(buf, sizeof(buf), "%s %s", ab_ui_tr("Version"), REV[0] != 0 ? REV : "-");
		ab_text(&c, cx, y, AB_UI_CENTER, buf, (int)(24 * s), ab_col_accent);
		y += (int)(34 * s);
		ab_text(&c, cx, y, AB_UI_CENTER, "\xc2\xa9 2026 AutoBleem team", (int)(22 * s), ab_col_name);
		y += (int)(30 * s);
		snprintf(buf, sizeof(buf), "%s  \xc2\xb7  %s  \xc2\xb7  %s", ab_cpu_name(), ab_gpu_name(), __DATE__);
		ab_text(&c, cx, y, AB_UI_CENTER, buf, (int)(18 * s), ab_col_dim);
		y += (int)(40 * s);
		ab_ui_fill(&c, px0 + (int)(80 * s), y, pw - (int)(160 * s), s >= 2 ? (int)s : 1, 0, ab_col_accent, 255);
		y += (int)(22 * s);
		ab_text(&c, cx, y, AB_UI_CENTER, ab_ui_tr("Based on"), (int)(20 * s), ab_col_accent);
		y += (int)(30 * s);

		/* upstream's credits, a line each; its blank lines are half a line; its leading spaces go */
		for (p = upstream_credits; *p; ) {
			const char *e = strchr(p, '\n');
			int len = e ? (int)(e - p) : (int)strlen(p);
			while (len > 0 && *p == ' ')
				p++, len--;
			if (len == 0) {
				y += (int)(10 * s);
			} else {
				snprintf(buf, sizeof(buf), "%.*s", len, p);
				ab_text(&c, cx, y, AB_UI_CENTER, buf, (int)(18 * s), ab_col_dim);
				y += (int)(23 * s);
			}
			if (e == NULL)
				break;
			p = e + 1;
		}

		ab_footer(&c, ab_ui_tr("OK"), ab_ui_tr("Back"));
		menu_draw_end();
		inp = in_menu_wait(PBTN_MOK|PBTN_MBACK|PBTN_MENU, NULL, 70);
		if (ab_console_power_off_requested || (inp & (PBTN_MOK|PBTN_MBACK|PBTN_MENU)))
			break;
	}
	ab_wait_released();
}

/* me_loop_d() over our screen: the same keys, the same handler contract (1 from a handler = leave the
 * menu, which is the game going on or the run ending) */
static int ab_menu_run(menu_entry *menu, int *menu_sel)
{
	int ret = 0, inp, sel = *menu_sel, sel_max;

	sel_max = me_count(menu) - 1;
	if (sel_max < 0)
		return 0;
	if (menu == e_menu_ab && ab_row_tab(menu, sel) != ab_tab)
		sel = ab_tab_sel[ab_tab] >= 0 ? ab_tab_sel[ab_tab] : 0;
	while ((!menu[sel].enabled || !menu[sel].selectable || !ab_in_tab(menu, sel)) && sel < sel_max)
		sel++;

	ab_menu_draw(menu, sel);
	ab_wait_released();
	for (;;) {
		ab_menu_draw(menu, sel);
		inp = in_menu_wait(PBTN_UP|PBTN_DOWN|PBTN_LEFT|PBTN_RIGHT|
			PBTN_MOK|PBTN_MBACK|PBTN_MENU|PBTN_L|PBTN_R, NULL, 70);
		if (ab_console_power_off_requested || (inp & (PBTN_MENU|PBTN_MBACK)))
			break;
		if (menu == e_menu_ab && (inp & (PBTN_L|PBTN_R))) {
			/* L1 the tab to the left, R1 to the right (round), each keeping its row */
			ab_tab_sel[ab_tab] = sel;
			ab_tab = (ab_tab + ((inp & PBTN_R) ? 1 : AB_TABS - 1)) % AB_TABS;
			sel = ab_tab_sel[ab_tab] >= 0 ? ab_tab_sel[ab_tab] : 0;
			while ((!menu[sel].enabled || !menu[sel].selectable || !ab_in_tab(menu, sel)) && sel < sel_max)
				sel++;
			continue;
		}
		if (inp & PBTN_UP) {
			do {
				if (--sel < 0)
					sel = sel_max;
			} while (!menu[sel].enabled || !menu[sel].selectable || !ab_in_tab(menu, sel));
		}
		if (inp & PBTN_DOWN) {
			do {
				if (++sel > sel_max)
					sel = 0;
			} while (!menu[sel].enabled || !menu[sel].selectable || !ab_in_tab(menu, sel));
		}
		if (ab_row_blocked(&menu[sel]) != NULL)
			continue;	/* greyed: its value does not move (the help says why) */
		if (inp & (PBTN_LEFT|PBTN_RIGHT|PBTN_L|PBTN_R)) {
			if (me_process(&menu[sel], (inp & (PBTN_RIGHT|PBTN_R)) ? 1 : 0, inp & (PBTN_L|PBTN_R)))
				continue;
		}
		if (inp & (PBTN_MOK|PBTN_LEFT|PBTN_RIGHT|PBTN_L|PBTN_R)) {
			/* a plain row takes Cross alone; a value row with a handler takes the arrows too */
			if (menu[sel].handler != NULL && (menu[sel].beh != MB_NONE || (inp & PBTN_MOK))) {
				int depth = ab_crumb_n;
				if (depth < AB_MENU_MAX_DEPTH)
					ab_crumbs[ab_crumb_n++] = menu[sel].name;
				ret = menu[sel].handler(menu[sel].id, inp);
				ab_crumb_n = depth;
				if (ret)
					break;
				sel_max = me_count(menu) - 1;
			}
		}
	}
	*menu_sel = sel;
	if (menu == e_menu_ab)
		ab_tab_sel[ab_tab] = sel;
	return ret;
}

/* ---- the cheats: the list of the loaded file's cheats and the .cht file picker, on our menu screen ---- */

#define AB_CHEAT_ID   10000		/* a cheat row's id: this + its index (clear of every MA_ id) */
#define AB_PICK_ID    20000		/* a picker row's id: this + its index */
#define AB_PICK_MAX   256

static const char h_ab_cheat[] = "Left/Right or Cross switches the cheat; it works while the game runs";
static const char h_ab_cht_file[] = "Reads this file's cheats (PCSX's .cht format: [name] lines, each with its codes under it)";
static const char h_ab_cht_dir[] = "Opens this folder";
static const char h_ab_cht_up[] = "The folder above";

static int ab_cheat_handler(int id, int keys)
{
	int i = id - AB_CHEAT_ID;
	if ((keys & PBTN_MOK) && i >= 0 && i < NumCheats)
		Cheats[i].Enabled = !Cheats[i].Enabled;
	return 0;
}

/* menu_loop_cheats() under PSCLASSIC: each cheat an ON/OFF row */
static void ab_cheat_list(void)
{
	static int sel = 0;
	menu_entry *m;
	int i;

	if (NumCheats <= 0)
		return;
	m = calloc(NumCheats + 1, sizeof(*m));
	if (m == NULL)
		return;
	for (i = 0; i < NumCheats; i++) {
		m[i].name = Cheats[i].Descr != NULL ? Cheats[i].Descr : "?";
		m[i].beh = MB_OPT_ONOFF;
		m[i].id = AB_CHEAT_ID + i;
		m[i].var = &Cheats[i].Enabled;
		m[i].mask = 1;
		m[i].enabled = 1;
		m[i].selectable = 1;
		m[i].handler = ab_cheat_handler;
		m[i].help = h_ab_cheat;
	}
	if (sel >= NumCheats)
		sel = 0;
	ab_debug_screen("cheats");
	ab_menu_run(m, &sel);
	free(m);
}

static char ab_pick_dir[MAXPATHLEN];
static char *ab_pick_names[AB_PICK_MAX];	/* "[folder]" or the file's name */
static int ab_pick_n, ab_pick_chosen;

static const char *ab_pick_empty_name(int id, int *offs)
{
	(void)id;
	(void)offs;
	return ab_ui_tr("No cheat files (.cht) in this folder");
}

static int ab_pick_handler(int id, int keys)
{
	if (keys & PBTN_MOK) {
		ab_pick_chosen = id - AB_PICK_ID;
		return 1;
	}
	return 0;
}

static int ab_pick_cmp(const void *a, const void *b)
{
	const char *x = *(char *const *)a, *y = *(char *const *)b;
	/* folders first */
	if ((x[0] == '[') != (y[0] == '['))
		return x[0] == '[' ? -1 : 1;
	return strcasecmp(x, y);
}

/* the folder's sub-folders and .cht files, sorted */
static void ab_pick_scan(void)
{
	DIR *d;
	struct dirent *e;
	struct stat st;
	char path[MAXPATHLEN + 256];

	while (ab_pick_n > 0)
		free(ab_pick_names[--ab_pick_n]);
	d = opendir(ab_pick_dir);
	if (d == NULL)
		return;
	while ((e = readdir(d)) != NULL && ab_pick_n < AB_PICK_MAX) {
		size_t len = strlen(e->d_name);
		char *s;
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", ab_pick_dir, e->d_name);
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			s = malloc(len + 3);
			if (s != NULL)
				sprintf(s, "[%s]", e->d_name);
		} else if (len > 4 && strcasecmp(e->d_name + len - 4, ".cht") == 0) {
			s = strdup(e->d_name);
		} else {
			continue;
		}
		if (s != NULL)
			ab_pick_names[ab_pick_n++] = s;
	}
	closedir(d);
	qsort(ab_pick_names, ab_pick_n, sizeof(ab_pick_names[0]), ab_pick_cmp);
}

/* the .cht file to read (a static path), or NULL: from the game's folder, into folders and up */
static const char *ab_cheat_file_pick(void)
{
	static char chosen[MAXPATHLEN + 256];
	static menu_entry m[AB_PICK_MAX + 3];
	const char *ret = NULL;
	int sel = 0;

	if (ab_pick_dir[0] == 0) {
		const char *iso = GetIsoFile();
		const char *p;
		snprintf(ab_pick_dir, sizeof(ab_pick_dir), "%s", iso != NULL && iso[0] ? iso : last_selected_fname);
		p = strrchr(ab_pick_dir, '/');
		if (p != NULL)
			ab_pick_dir[p - ab_pick_dir] = 0;
		else
			snprintf(ab_pick_dir, sizeof(ab_pick_dir), ".");
	}
	for (;;) {
		int i, k = 0, top = strchr(ab_pick_dir, '/') == NULL || strcmp(ab_pick_dir, "/") == 0;
		ab_pick_scan();
		memset(m, 0, sizeof(m));
		if (!top) {
			m[k].name = "[Parent folder]";
			m[k].id = AB_PICK_ID - 1;
			m[k].enabled = m[k].selectable = 1;
			m[k].handler = ab_pick_handler;
			m[k].help = h_ab_cht_up;
			k++;
		}
		for (i = 0; i < ab_pick_n; i++, k++) {
			m[k].name = ab_pick_names[i];
			m[k].id = AB_PICK_ID + i;
			m[k].enabled = m[k].selectable = 1;
			m[k].handler = ab_pick_handler;
			m[k].help = ab_pick_names[i][0] == '[' ? h_ab_cht_dir : h_ab_cht_file;
		}
		if (ab_pick_n == 0 || ab_pick_names[ab_pick_n - 1][0] == '[') {
			m[k].name = "";
			m[k].enabled = 1;
			m[k].generate_name = ab_pick_empty_name;
			k++;
		}
		if (top && ab_pick_n == 0) {
			/* nothing to pick and nowhere to go (ab_menu_run needs a selectable row) */
			menu_update_msg("No cheat files (.cht) in this folder");
			break;
		}
		if (sel >= k)
			sel = 0;
		ab_pick_chosen = -2;
		ab_debug_screen("cheatfile");
		if (!ab_menu_run(m, &sel) || ab_pick_chosen == -2)
			break;
		sel = 0;
		if (ab_pick_chosen == -1) {
			char *p = strrchr(ab_pick_dir, '/');
			if (p == ab_pick_dir)
				p[1] = 0;
			else if (p != NULL)
				*p = 0;
			continue;
		}
		if (ab_pick_names[ab_pick_chosen][0] == '[') {
			const char *nm = ab_pick_names[ab_pick_chosen];
			size_t len = strlen(ab_pick_dir);
			snprintf(ab_pick_dir + len, sizeof(ab_pick_dir) - len, "%s%.*s",
				len > 0 && ab_pick_dir[len - 1] == '/' ? "" : "/", (int)strlen(nm) - 2, nm + 1);
			continue;
		}
		snprintf(chosen, sizeof(chosen), "%s/%s", ab_pick_dir, ab_pick_names[ab_pick_chosen]);
		ret = chosen;
		break;
	}
	while (ab_pick_n > 0)
		free(ab_pick_names[--ab_pick_n]);
	return ret;
}

/* load_pcsx_cht() under PSCLASSIC: pick, read, say how many, show the list */
static void ab_cheat_load(void)
{
	static char msg[128];
	const char *fname = ab_cheat_file_pick();

	if (fname == NULL)
		return;
	printf("selected cheat file: %s\n", fname);
	LoadCheats(fname);
	if (NumCheats == 0 && NumCodes == 0) {
		menu_update_msg("No cheats in this file");
		return;
	}
	snprintf(msg, sizeof(msg), "%s %d", ab_ui_tr("Cheats loaded:"), NumCheats);
	menu_update_msg(msg);
	me_enable(e_menu_main, MA_MAIN_CHEATS, ready_to_go && NumCheats);
	ab_cheat_list();
}

static int ab_menu_pcsx_handler(int id, int keys)
{
	static int sel = 0;

	/* (only ids the PSCLASSIC table has: a missing one would hit the first row) */
	me_enable(e_menu_main, MA_MAIN_RESUME_GAME, ready_to_go);
	me_enable(e_menu_main, MA_MAIN_CHEATS,      ready_to_go && NumCheats);
	me_enable(e_menu_main, MA_MAIN_LOAD_CHEATS, ready_to_go);
	(void)main_menu2_handler;	/* Extra stuff is not in the table here */
	(void)h_extra;

	ab_debug_screen("pcsx");	/* upstream's menu and its pages, until we draw ours again */
	return ab_menu_run(e_menu_main, &sel);
}

/* menu_loop()'s loop: our menu at the top, until the game is to go on or the run is to end */
static void ab_menu_loop_d(void)
{
	static int sel = 0;

	me_enable(e_menu_ab, MA_MAIN_RESUME_GAME, ready_to_go);
	me_enable(e_menu_ab, MA_AB_QUICKSAVE, ready_to_go && CdromId[0]);
	me_enable(e_menu_ab, MA_AB_QUICKLOAD, ready_to_go && CdromId[0]);
	me_enable(e_menu_ab, MA_AB_AUTOLOAD,  ready_to_go && CdromId[0]);
	me_enable(e_menu_ab, MA_MAIN_RESET_GAME, ready_to_go);
	me_enable(e_menu_ab, MA_AB_DISC,      ready_to_go && CdromId[0]);
	me_enable(e_menu_ab, MA_AB_FILTER,    plat_target.hwfilters != NULL);
	e_menu_ab[me_id2offset(e_menu_ab, MA_MAIN_EXIT)].help = h_ab_exit;
#ifdef BUILTIN_GPU_NEON
	me_enable(e_menu_ab, MA_AB_ENHANCE, gpu_plugsel == 0);	/* 0 is "builtin_gpu" */
	me_enable(e_menu_ab, MA_AB_NOSEAMS, gpu_plugsel == 0);
#else
	me_enable(e_menu_ab, MA_AB_ENHANCE, 0);
	me_enable(e_menu_ab, MA_AB_NOSEAMS, 0);
#endif
	{
		/* the Display row: Auto, then every mode the display lists at 50 Hz or more (TV modes, then VESA) */
		int i, n = ab_output_modes(ab_display_modes, AB_OUTPUT_MAX_MODES);
		ab_display_sel = 0;
		for (i = 0; i < n; i++) {
			if (ab_display_modes[i] == ab_output_mode)
				ab_display_sel = i;
			ab_output_mode_name(ab_display_modes[i], ab_display_names[i], sizeof(ab_display_names[i]));
			men_ab_display[i] = ab_display_names[i];
		}
		men_ab_display[n] = NULL;
		me_enable(e_menu_ab, MA_AB_DISPLAY, n > 1);
		if (ab_console_present()) {
			/* the console: Weston's mode is set at boot (the launcher's setting, boot.sh), so the row
			 * only shows the output's resolution */
			static char cur[16];
			/* the menu's canvas is the output's size (plat_autobleem.c's resize_cb) */
			if (g_menuscreen_h == 1080 || g_menuscreen_h == 720)
				snprintf(cur, sizeof(cur), "%dp", g_menuscreen_h);
			else
				snprintf(cur, sizeof(cur), "%dx%d", g_menuscreen_w, g_menuscreen_h);
			men_ab_display[0] = cur;
			men_ab_display[1] = NULL;
			ab_display_modes[0] = ab_output_mode;
			ab_display_sel = 0;
			me_enable(e_menu_ab, MA_AB_DISPLAY, 1);
		}
	}
	if (ab_console_present()) {
		e_menu_ab[me_id2offset(e_menu_ab, MA_OPT_SWFILTER)].data = men_ab_smooth_psc;
		if (soft_filter > SOFT_FILTER_EAGLE2X)
			soft_filter = SOFT_FILTER_NONE;
	}

	ab_ui_load(ab_opts.language);
	{
		/* the console at 1080p: CRT-Pi stays, with a word that it is too heavy there (the owner, 2026-09-28);
		 * the help is drawn through ab_ui_tr, which gives an already translated text back as it is */
		static char help[512];
		menu_entry *f = &e_menu_ab[me_id2offset(e_menu_ab, MA_AB_FILTER)];
		if (ab_console_present() && g_menuscreen_h >= 1080) {
			snprintf(help, sizeof(help), "%s. %s", ab_ui_tr(h_ab_filter), ab_ui_tr(h_ab_crtpi_1080));
			f->help = help;
		} else {
			f->help = h_ab_filter;
		}
	}
	ab_menu_prepare_bg();
	ab_snap_take();		/* the frame the game was on when the menu opened */
	do {
		ab_rows_take();
		ab_menu_run(e_menu_ab, &sel);
		ab_rows_commit();
	} while (!ready_to_go && !g_emu_want_quit);
	/* the output mode last: switching it resizes the canvas the menu drew on */
	if (ab_display_modes[ab_display_sel] != ab_output_mode && !g_emu_want_quit)
		ab_output_mode_apply(ab_display_modes[ab_display_sel], 1);
}
