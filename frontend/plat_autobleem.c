/*
 * The AutoBleem platform of pcsx-abnxt: one window with a GL context on every target (Wayland on the
 * PlayStation Classic, KMSDRM on a Raspberry Pi, the desktop on a PC), the emulator's frame and the menu
 * drawn by our own GL passes (frontend/libpicofe/plat_autobleem.c, docs/render-pipeline-plan.md), the
 * keyboard and the game controllers through libpicofe's in_sdl2 / in_sdl2gc drivers. Replaces plat_sdl.c
 * (upstream's SDL 1.2 platform, which stays in the tree untouched) for the AutoBleem targets.
 *
 * (C) Gražvydas "notaz" Ignotas, 2011-2013 (the plat_sdl.c this follows)
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of any of these licenses (at your option):
 *  - GNU GPL, version 2 or later.
 *  - GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <SDL.h>

#include "../libpcsxcore/plugins.h"
#include "libpicofe/input.h"
#include "libpicofe/in_sdl2.h"
#include "libpicofe/in_sdl2gc.h"
#include "libpicofe/menu.h"
#include "libpicofe/fonts.h"
#include "libpicofe/plat_autobleem.h"
#include "libpicofe/plat.h"
#include "cspace.h"
#include "plugin_lib.h"
#include "plugin.h"
#include "menu.h"
#include "main.h"
#include "plat.h"
#include "revision.h"
#include "ab/ab_config.h"
#include "ab/ab_debug.h"
#include "ab/ab_headless.h"
#include "ab/ab_pad_battery.h"
#include "ab/ab_hud_text.h"
#include "ab/ab_shaders.h"
#include "ab/ab_console.h"
#include "ab/ab_ui.h"

/* the keyboard: the same keys upstream's SDL 1.2 platform binds, by scancode */
static const struct in_default_bind in_sdl2_defbinds[] = {
  { SDL_SCANCODE_UP,     IN_BINDTYPE_PLAYER12, DKEY_UP },
  { SDL_SCANCODE_DOWN,   IN_BINDTYPE_PLAYER12, DKEY_DOWN },
  { SDL_SCANCODE_LEFT,   IN_BINDTYPE_PLAYER12, DKEY_LEFT },
  { SDL_SCANCODE_RIGHT,  IN_BINDTYPE_PLAYER12, DKEY_RIGHT },
  { SDL_SCANCODE_D,      IN_BINDTYPE_PLAYER12, DKEY_TRIANGLE },
  { SDL_SCANCODE_Z,      IN_BINDTYPE_PLAYER12, DKEY_CROSS },
  { SDL_SCANCODE_X,      IN_BINDTYPE_PLAYER12, DKEY_CIRCLE },
  { SDL_SCANCODE_S,      IN_BINDTYPE_PLAYER12, DKEY_SQUARE },
  { SDL_SCANCODE_V,      IN_BINDTYPE_PLAYER12, DKEY_START },
  { SDL_SCANCODE_C,      IN_BINDTYPE_PLAYER12, DKEY_SELECT },
  { SDL_SCANCODE_W,      IN_BINDTYPE_PLAYER12, DKEY_L1 },
  { SDL_SCANCODE_R,      IN_BINDTYPE_PLAYER12, DKEY_R1 },
  { SDL_SCANCODE_E,      IN_BINDTYPE_PLAYER12, DKEY_L2 },
  { SDL_SCANCODE_T,      IN_BINDTYPE_PLAYER12, DKEY_R2 },
  { SDL_SCANCODE_ESCAPE, IN_BINDTYPE_EMU, SACTION_ENTER_MENU },
  { SDL_SCANCODE_F1,     IN_BINDTYPE_EMU, SACTION_SAVE_STATE },
  { SDL_SCANCODE_F2,     IN_BINDTYPE_EMU, SACTION_LOAD_STATE },
  { SDL_SCANCODE_F3,     IN_BINDTYPE_EMU, SACTION_PREV_SSLOT },
  { SDL_SCANCODE_F4,     IN_BINDTYPE_EMU, SACTION_NEXT_SSLOT },
  { SDL_SCANCODE_F5,     IN_BINDTYPE_EMU, SACTION_TOGGLE_FSKIP },
  { SDL_SCANCODE_F6,     IN_BINDTYPE_EMU, SACTION_SCREENSHOT },
  { SDL_SCANCODE_F7,     IN_BINDTYPE_EMU, SACTION_TOGGLE_FPS },
  { SDL_SCANCODE_F8,     IN_BINDTYPE_EMU, SACTION_SWITCH_DISPMODE },
  { SDL_SCANCODE_F11,    IN_BINDTYPE_EMU, SACTION_TOGGLE_FULLSCREEN },
  { SDL_SCANCODE_BACKSPACE, IN_BINDTYPE_EMU, SACTION_FAST_FORWARD },
  /* the console's front buttons as its kernel sends them (the launcher's pcsx.cfg binds them too), and
   * F9/F10 for a keyboard without them: Open = the disc picker, Reset = out with the resume point */
  { SDL_SCANCODE_EJECT,     IN_BINDTYPE_EMU, SACTION_AB_CD_CHANGE },
  { SDL_SCANCODE_F9,        IN_BINDTYPE_EMU, SACTION_AB_CD_CHANGE },
  { SDL_SCANCODE_AUDIOPLAY, IN_BINDTYPE_EMU, SACTION_AB_RESET },
  { SDL_SCANCODE_F10,       IN_BINDTYPE_EMU, SACTION_AB_RESET },
  { 0, 0, 0 }
};

static const struct menu_keymap in_sdl2_key_map[] =
{
  { SDL_SCANCODE_UP,     PBTN_UP },
  { SDL_SCANCODE_DOWN,   PBTN_DOWN },
  { SDL_SCANCODE_LEFT,   PBTN_LEFT },
  { SDL_SCANCODE_RIGHT,  PBTN_RIGHT },
  { SDL_SCANCODE_RETURN, PBTN_MOK },
  { SDL_SCANCODE_ESCAPE, PBTN_MBACK },
  { SDL_SCANCODE_SEMICOLON,    PBTN_MA2 },
  { SDL_SCANCODE_APOSTROPHE,   PBTN_MA3 },
  { SDL_SCANCODE_LEFTBRACKET,  PBTN_L },
  { SDL_SCANCODE_RIGHTBRACKET, PBTN_R },
};

static const struct in_pdata in_sdl2_platform_data = {
  .defbinds  = in_sdl2_defbinds,
  .key_map   = in_sdl2_key_map,
  .kmap_size = sizeof(in_sdl2_key_map) / sizeof(in_sdl2_key_map[0]),
};

/* the pads: the driver's PlayStation-named buttons onto the emulator's - the same on every pad */
static const struct in_default_bind in_sdl2gc_defbinds[] = {
  { SDL2GC_DPAD_UP,      IN_BINDTYPE_PLAYER12, DKEY_UP },
  { SDL2GC_DPAD_DOWN,    IN_BINDTYPE_PLAYER12, DKEY_DOWN },
  { SDL2GC_DPAD_LEFT,    IN_BINDTYPE_PLAYER12, DKEY_LEFT },
  { SDL2GC_DPAD_RIGHT,   IN_BINDTYPE_PLAYER12, DKEY_RIGHT },
  { SDL2GC_BTN_TRIANGLE, IN_BINDTYPE_PLAYER12, DKEY_TRIANGLE },
  { SDL2GC_BTN_CROSS,    IN_BINDTYPE_PLAYER12, DKEY_CROSS },
  { SDL2GC_BTN_CIRCLE,   IN_BINDTYPE_PLAYER12, DKEY_CIRCLE },
  { SDL2GC_BTN_SQUARE,   IN_BINDTYPE_PLAYER12, DKEY_SQUARE },
  { SDL2GC_BTN_START,    IN_BINDTYPE_PLAYER12, DKEY_START },
  { SDL2GC_BTN_SELECT,   IN_BINDTYPE_PLAYER12, DKEY_SELECT },
  { SDL2GC_BTN_L1,       IN_BINDTYPE_PLAYER12, DKEY_L1 },
  { SDL2GC_BTN_R1,       IN_BINDTYPE_PLAYER12, DKEY_R1 },
  { SDL2GC_BTN_L2,       IN_BINDTYPE_PLAYER12, DKEY_L2 },
  { SDL2GC_BTN_R2,       IN_BINDTYPE_PLAYER12, DKEY_R2 },
  { SDL2GC_BTN_L3,       IN_BINDTYPE_PLAYER12, DKEY_L3 },
  { SDL2GC_BTN_R3,       IN_BINDTYPE_PLAYER12, DKEY_R3 },
  { SDL2GC_BTN_PS,       IN_BINDTYPE_EMU,      SACTION_ENTER_MENU },
  { 0, 0, 0 }
};

static const struct in_pdata in_sdl2gc_platform_data = {
  .defbinds  = in_sdl2gc_defbinds,
};

/* where a gamecontrollerdb.txt may be: the AutoBleem kernel's, the launcher's next to the emulator's
 * resources, one in the working directory */
static const char * const controller_db_files[] = {
  "/etc/autobleem/gamecontrollerdb.txt",
  "/media/Autobleem/bin/autobleem/gamecontrollerdb.txt",
  "gamecontrollerdb.txt",
  NULL
};

static int psx_w = 256, psx_h = 240;	/* the emulator's output as plat_gvideo_set_mode() was told it */
static void *shadow_fb;			/* the frame the GPU plugin draws into, RGB565 */
static void *menu_fb;			/* what the menu draws into, RGB565, the window's size */
static void *menubg_img;		/* the last frame, the menu's background */
static int in_menu;
static int fullscreen_old;

static void quit_cb(void)
{
  emu_core_ask_exit();
}

/* the window's output size changed: the menu's canvas and the layer the frame is scaled into follow */
static void resize_cb(int w, int h)
{
  g_menuscreen_w = w;
  g_menuscreen_h = h;
  g_menuscreen_pp = w;
  free(menu_fb);
  menu_fb = calloc(w * h, 2);
  if (menu_fb == NULL) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  pl_update_layer_size(psx_w, psx_h, w, h);
  if (in_menu)
    g_menuscreen_ptr = menu_fb;
  /* libpicofe's background buffers are the canvas' size too (menu_init() made them at the first size;
   * before it they are NULL and it makes them); menu_leave_emu() writes g_menuscreen_w * h into them */
  if (g_menubg_ptr != NULL) {
    free(g_menubg_ptr);
    free(g_menubg_src_ptr);
    g_menubg_ptr = calloc(w * h, 2);
    g_menubg_src_ptr = calloc(w * h, 2);
    if (g_menubg_ptr == NULL || g_menubg_src_ptr == NULL) {
      fprintf(stderr, "OOM\n");
      exit(1);
    }
  }
}

/* C11, round 3 (Marcus's review): the one place pad_order is kept once pads_changed() computes it, so
 * plat_trigger_vibrate() below can use the exact same mapping the analog sticks (in_adev[], right here in
 * pads_changed()) and the buttons (ab_pads_swapped, plugin_lib.c) use - one source of truth for "which
 * physical pad (SDL acceptance index) is on which PS1 port right now". Starts unswapped for the window
 * before the first pads_changed() call (never observed in practice - it runs from in_sdl2gc_init()). */
static int ab_pad_order_state[2] = { 0, 1 };

/* every pad is its player's analog sticks too: in_adev[0]/[1] player 1's left/right, [2]/[3] player 2's */
static void pads_changed(int pad_count)
{
  int p;
  /* C11: Options -> "Swap Player 1 / Player 2" - a purely positional swap of the first two SDL pads'
   * PS1 ports. pad_order[] (in_sdl2gc's own acceptance-order "player" numbering, 1-based, is untouched -
   * we do not edit the libpicofe submodule here) is one of only two permutations of {0, 1}: the identity
   * or a single transposition, and both are their own inverse, so pad_order[p] + 1 is exactly the player
   * number that belongs at PS1 port p (0-based) whichever way round it is set. AB_PAD_ORDER unset, or
   * this build's abfeatures never having offered "padorder", leaves pad_order == {0, 1} and this loop
   * behaves exactly as before.
   *
   * Review fix (Marcus): the swap only takes effect with two or more pads connected. pad_count is exactly
   * in_sdl2gc's accepted-controller count (in_sdl2gc.c's in_sdl2gc_probe(), capped at SDL2GC_MAX_PADS), so
   * with one pad or none the identity order is forced here regardless of AB_PAD_ORDER - a lone pad always
   * lands on PS1 port 1 (player 1), swap on or off. This runs on every hot-plug re-probe too: unplugging
   * one of two pads mid-game drops pad_count to 1 and un-swaps the remaining pad onto port 1.
   *
   * Round 3: this only ever moved the analog sticks (in_adev[] below) - the digital buttons follow
   * libpicofe's own state->player (in_sdl2gc.c:296), untouched by pad_order, so a swapped pad's buttons
   * used to stay on their unswapped port while its sticks moved. ab_pad_order_state (this file) and
   * ab_pads_swapped (plugin_lib.c) are set here, right after pad_order is computed, so the buttons
   * (plugin_lib.c's update_input()) and the rumble call (plat_trigger_vibrate() below) can be kept in step
   * with the sticks without touching the libpicofe submodule. */
  int pad_order[2] = { 0, 1 };
  if (pad_count >= 2)
    ab_pad_order(pad_order);
  ab_pad_order_state[0] = pad_order[0];
  ab_pad_order_state[1] = pad_order[1];
  ab_pads_swapped = pad_count >= 2 && pad_order[0] != 0;
  for (p = 0; p < 2; p++) {
    int dev = in_sdl2gc_dev_id(pad_order[p] + 1);
    in_adev[p * 2] = in_adev[p * 2 + 1] = dev;
    in_adev_axis[p * 2][0] = SDL2GC_AXIS_LX; in_adev_axis[p * 2][1] = SDL2GC_AXIS_LY;
    in_adev_axis[p * 2 + 1][0] = SDL2GC_AXIS_RX; in_adev_axis[p * 2 + 1][1] = SDL2GC_AXIS_RY;
    in_adev_is_nublike[p * 2] = in_adev_is_nublike[p * 2 + 1] = 0;
  }
  printf("plat_ab: %d pad(s)\n", pad_count);
}

static void sdl_event_handler(void *event_)
{
  plat_ab_event_handler(event_);
}

static void get_layer_pos(int *x, int *y, int *w, int *h)
{
  *x = g_layer_x;
  *y = g_layer_y;
  *w = g_layer_w;
  *h = g_layer_h;
}

static void plugin_update(void)
{
  // used by some plugins...
  pl_rearmed_cbs.screen_w = plat_ab_win_w;
  pl_rearmed_cbs.screen_h = plat_ab_win_h;
  plugin_call_rearmed_cbs();
}

/* The HUD's slots in libpicofe's plat_autobleem (plat_ab_hud_image/draw): each image is uploaded when it
 * changes, into a texture made once, and drawn at screen pixels over the scanlines. */
enum { HUD_SLOT_BATTERY, HUD_SLOT_MSG, HUD_SLOT_CPU, HUD_SLOT_CHANS };

/* every HUD element's whole-pixel scale and margin, from the screen's height (2x at 480, 3x at 720) - the
 * HUD belongs to the screen, not to the picture, so the aspect ratio changes nothing */
static int hud_scale(int screen_h)
{
  int scale = screen_h / 240;
  return scale < 2 ? 2 : scale > 6 ? 6 : scale;
}

#ifdef PSCLASSIC
/* EMU-15: the low-battery icon, in the top-right corner of the screen, only while a pad's battery is low
 * (the show-while-held of the menu button is gone: Home opens the menu and, held, leaves the game; the
 * menu shows every pad's battery). */
#define AB_HUD_ICON_W    22
#define AB_HUD_ICON_H    11
#define AB_HUD_NUB_W     2
#define AB_HUD_TEX_W     (AB_HUD_ICON_W + AB_HUD_NUB_W + 2)  /* +2: the 1 px backing border each side */
#define AB_HUD_TEX_H     (AB_HUD_ICON_H + 2)
#define AB_HUD_ARGB_BACKING  0xff000000u
#define AB_HUD_ARGB_OUTLINE  0xffffffffu
#define AB_HUD_ARGB_LOW      0xffff0000u  /* red:   <= the low threshold */
#define AB_HUD_ARGB_MED      0xffff6500u  /* amber: <= half */
#define AB_HUD_ARGB_OK       0xff00ff00u  /* green: a healthy percent (a plain show-request only) */

static void ab_hud_battery_set(Uint32 *px, int x, int y, Uint32 argb)
{
  if ((unsigned)x >= (unsigned)AB_HUD_TEX_W || (unsigned)y >= (unsigned)AB_HUD_TEX_H)
    return;
  px[y * AB_HUD_TEX_W + x] = argb;
}

static void ab_hud_battery_fill(Uint32 *px, int x0, int y0, int x1, int y1, Uint32 argb)
{
  int x, y;
  for (y = y0; y < y1; y++)
    for (x = x0; x < x1; x++)
      ab_hud_battery_set(px, x, y, argb);
}

/* fills the AB_HUD_TEX_W x AB_HUD_TEX_H ARGB8888 buffer with the icon for this percent - the same shape
 * plugin_lib.c's old draw_pad_battery_icon drew, one pixel bigger all around for the backing border */
static void ab_hud_battery_build(Uint32 *px, int percent)
{
  int ox = 1, oy = 1, bw = AB_HUD_ICON_W, bh = AB_HUD_ICON_H;
  int nub_x0, nub_y0, nub_y1, fill_w;
  Uint32 fill_color = percent <= AB_PAD_BATTERY_LOW_PERCENT ? AB_HUD_ARGB_LOW :
                      percent <= 50 ? AB_HUD_ARGB_MED : AB_HUD_ARGB_OK;

  /* solid backing so the outline reads over a bright game frame, then the outline itself */
  ab_hud_battery_fill(px, ox - 1, oy - 1, ox + bw + AB_HUD_NUB_W + 1, oy + bh + 1, AB_HUD_ARGB_BACKING);
  ab_hud_battery_fill(px, ox, oy, ox + bw, oy + 1, AB_HUD_ARGB_OUTLINE);
  ab_hud_battery_fill(px, ox, oy + bh - 1, ox + bw, oy + bh, AB_HUD_ARGB_OUTLINE);
  ab_hud_battery_fill(px, ox, oy, ox + 1, oy + bh, AB_HUD_ARGB_OUTLINE);
  ab_hud_battery_fill(px, ox + bw - 1, oy, ox + bw, oy + bh, AB_HUD_ARGB_OUTLINE);

  /* the nub on the right, a third of the body's height, centred */
  nub_x0 = ox + bw;
  nub_y0 = oy + bh / 3;
  nub_y1 = oy + bh - bh / 3;
  ab_hud_battery_fill(px, nub_x0, nub_y0, nub_x0 + AB_HUD_NUB_W, nub_y1, AB_HUD_ARGB_OUTLINE);

  /* the fill itself, proportional to the percent, inset one pixel inside the outline */
  fill_w = (bw - 4) * percent / 100;
  if (fill_w > 0)
    ab_hud_battery_fill(px, ox + 2, oy + 2, ox + 2 + fill_w, oy + bh - 2, fill_color);
}

static void ab_hud_battery_draw(int sw, int sh)
{
  static int icon_percent = -2;  /* not a real percent: forces the first upload */
  int percent, scale, margin;
  SDL_Rect r;

  if (!ab_pad_battery_visible())
    return;
  percent = ab_pad_battery_percent();
  if (percent < 0)
    return;

  if (icon_percent != percent) {
    Uint32 px[AB_HUD_TEX_W * AB_HUD_TEX_H];
    memset(px, 0, sizeof(px));  /* transparent black */
    ab_hud_battery_build(px, percent);
    if (plat_ab_hud_image(HUD_SLOT_BATTERY, px, AB_HUD_TEX_W, AB_HUD_TEX_H) != 0)
      return;
    icon_percent = percent;
  }
  scale = hud_scale(sh);
  margin = 6 * scale / 2;
  r.w = AB_HUD_TEX_W * scale;
  r.h = AB_HUD_TEX_H * scale;
  r.x = sw - r.w - margin;
  r.y = margin;
  plat_ab_hud_draw(HUD_SLOT_BATTERY, &r);
}
#endif

/* EMU-15 part 2: hud_msg/FPS/CPU load/the SPU channel bar, at the bottom of the screen (the message/FPS
 * line left, CPU load right, the channel bar in the middle), on every platform. The text is the menus' TTF
 * font (ab_ui.c) on a dark translucent strip, at the screen's pixels; without a font, ab_hud_text.c's 8x8
 * glyphs scaled up. A line is rendered and uploaded to its slot only when its text changes. */

#define HUD_LINE_MAX_W 1280
#define HUD_LINE_MAX_H 64

/* a text line's slot: the text it holds and the size it is drawn at, in screen pixels */
typedef struct {
  int slot;
  char text[96];
  int w, h;
} AbHudLine;

/* the line's image: TTF at `size` px on a strip, or the 8x8 font; 0 = none */
static int ab_hud_line_render(Uint32 *px, const char *text, int size, int scale, int *w, int *h)
{
  if (ab_ui_has_font()) {
    int pad = size / 3, tw = ab_ui_text_width(text, size), x, y;
    *w = tw + pad * 2;
    *h = size + pad;
    if (tw <= 0 || *w > HUD_LINE_MAX_W || *h > HUD_LINE_MAX_H)
      return 0;
    for (y = 0; y < *h; y++)
      for (x = 0; x < *w; x++)
        px[y * *w + x] = 0x90000000u;   /* the strip */
    ab_ui_text_argb(px, *w, *h, pad + 1, pad / 2 + 1, text, size, 0xff000000u);   /* a shadow */
    ab_ui_text_argb(px, *w, *h, pad, pad / 2, text, size, 0xffffffffu);
    return 1;
  }
  *w = ab_hud_text_width(text);
  *h = AB_HUD_GLYPH_H;
  if (*w <= 0 || *w > HUD_LINE_MAX_W)
    return 0;
  memset(px, 0, (size_t)*w * *h * 4);  /* transparent black */
  ab_hud_text_render_argb(px, *w, *h, text);
  *w *= scale;   /* drawn scaled up */
  *h *= scale;
  return 1;
}

/* 1 when the line has something to draw (rendered and uploaded now if the text changed) */
static int ab_hud_line_update(AbHudLine *line, const char *text, int sh)
{
  static Uint32 px[HUD_LINE_MAX_W * HUD_LINE_MAX_H];
  int scale = hud_scale(sh), size = sh / 28, w, h;

  if (text == NULL || text[0] == 0)
    return 0;
  if (line->w > 0 && strcmp(line->text, text) == 0)
    return 1;
  ab_ui_load(ab_opts.language);   /* the font and the strings, once (the menu loads them too) */
  if (size < 12)
    size = 12;
  if (!ab_hud_line_render(px, text, size, scale, &w, &h))
    return 0;
  if (plat_ab_hud_image(line->slot, px, ab_ui_has_font() ? w : w / scale, ab_ui_has_font() ? h : h / scale) != 0)
    return 0;
  snprintf(line->text, sizeof(line->text), "%s", text);
  line->w = w;
  line->h = h;
  return 1;
}

static void ab_hud_notices_draw(int sw, int sh)
{
  static AbHudLine msg_line = { HUD_SLOT_MSG }, cpu_line = { HUD_SLOT_CPU };
  static unsigned short chans_cache[AB_HUD_CHANS_N];
  static int chans_cached_n = -1;
  unsigned short colors[AB_HUD_CHANS_N];
  int scale = hud_scale(sh), margin = 6 * scale / 2, n;
  SDL_Rect r;

  if (ab_hud_line_update(&msg_line, ab_hud_msg_line(), sh)) {
    r.w = msg_line.w;
    r.h = msg_line.h;
    r.x = margin;
    r.y = sh - r.h - margin;
    plat_ab_hud_draw(HUD_SLOT_MSG, &r);
  }
  if (ab_hud_line_update(&cpu_line, ab_hud_cpu_line(), sh)) {
    r.w = cpu_line.w;
    r.h = cpu_line.h;
    r.x = sw - r.w - margin;
    r.y = sh - r.h - margin;
    plat_ab_hud_draw(HUD_SLOT_CPU, &r);
  }

  n = ab_hud_active_chans(colors, AB_HUD_CHANS_N);
  if (n <= 0) {
    chans_cached_n = -1;  /* Show SPU channels turned off: upload again if it's turned back on */
    return;
  }
  if (n != chans_cached_n || memcmp(colors, chans_cache, n * sizeof(colors[0])) != 0) {
    Uint32 px[AB_HUD_CHANS_N * AB_HUD_GLYPH_W * AB_HUD_GLYPH_H];
    int c, x, y;
    for (c = 0; c < n; c++) {
      Uint32 argb = ab_hud_rgb565_to_argb(colors[c]);
      for (y = 0; y < AB_HUD_GLYPH_H; y++)
        for (x = 0; x < AB_HUD_GLYPH_W; x++)
          px[y * (n * AB_HUD_GLYPH_W) + c * AB_HUD_GLYPH_W + x] = argb;
    }
    if (plat_ab_hud_image(HUD_SLOT_CHANS, px, n * AB_HUD_GLYPH_W, AB_HUD_GLYPH_H) != 0)
      return;
    memcpy(chans_cache, colors, n * sizeof(colors[0]));
    chans_cached_n = n;
  }
  r.w = n * AB_HUD_GLYPH_W * scale;
  r.h = AB_HUD_GLYPH_H * scale;
  r.x = sw / 2 - r.w / 2;
  r.y = sh - r.h - margin;
  plat_ab_hud_draw(HUD_SLOT_CHANS, &r);
}

/* the HUD callback: the battery icon (PSCLASSIC only, as it always was) plus the notices (every platform) */
static void ab_hud_draw(int sw, int sh)
{
#ifdef PSCLASSIC
  ab_hud_battery_draw(sw, sh);
#endif
  ab_hud_notices_draw(sw, sh);
}

/* soft_filter values the GPU smooths (plugin_lib then keeps the frame 1x) */
static int plat_smooths(int soft_filter)
{
  return ab_smooth_shader(soft_filter) != NULL;
}

/* the pipeline for this frame from the menus' settings: the filter, the smoothing (not with a CRT on the
 * console - its GPU cannot do both), and our scanlines (not with a CRT, which draws its own) */
static void update_pipeline(void)
{
  static const int pattern[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 2 }, { 2, 1 } };  /* dark, bright rows */
  int filter = plat_target.hwfilter, crt, level;
  const struct plat_ab_shader *smooth;
  static int console = -1;

  if (filter < 0 || filter >= AB_FILTER_COUNT)
    filter = AB_FILTER_LINEAR;
  if (console < 0)
    console = ab_console_present();
  crt = ab_filter_is_crt(filter);
  if (console && soft_filter > SOFT_FILTER_EAGLE2X)
    soft_filter = SOFT_FILTER_NONE;	/* hq2x/hq3x are not offered on the console (a cfg from before) */
  smooth = ab_smooth_shader(soft_filter);
  if (console && crt)
    smooth = NULL;
  plat_ab_set_pipeline(smooth, ab_filter_shader(filter));
  level = crt ? 0 : scanlines < 0 ? 0 : scanlines > 3 ? 3 : scanlines;
  plat_ab_set_scanlines(pattern[level][0], pattern[level][1], (100 - scanline_level) * 255 / 100);
}

void plat_init(void)
{
  int fullscreen, ret, headless;

  plat_ab_quit_cb = quit_cb;
  plat_ab_resize_cb = resize_cb;
  pl_scanlines_by_plat = 1;
  /* EMU-15 part 2: this platform draws hud_msg/FPS/CPU load/the SPU channel bar itself, in ab_hud_draw()
   * below (registered a few lines down), after its own scanlines - plugin_lib.c's print_hud() must not
   * also draw them into pl_vout_buf, or a platform with both paths active would show every notice twice
   * (once dim/hidden under the scanlines, once correctly over them). Every other platform never sets
   * this and keeps print_hud()'s original behaviour untouched. */
  pl_hud_by_plat = 1;
  pl_plat_smooths = plat_smooths;	/* scale2x/eagle2x on the GPU: the frame stays 1x */

  /* AB_HEADLESS=1: the same automated-test-run policy as the launcher's Platform (autobleem-core's
   * ableem::Platform) - dummy audio (must be in the environment before SDL's audio subsystem inits,
   * which happens inside plat_ab_init() below) and a window hidden right after it is created, before
   * the first frame is ever presented, so a tester's desktop never sees it flash up. SDL_setenv, not
   * POSIX setenv: MinGW (the Windows build) has no setenv, and SDL_setenv has been in SDL since 2.0.0 -
   * well under the console's 2.0.14 ceiling - so this stays one line on every platform, read back by
   * SDL's own audio init through SDL_getenv. */
  headless = ab_headless_requested();
  if (ab_headless_audio_driver(headless) != NULL)
    SDL_setenv("SDL_AUDIODRIVER", ab_headless_audio_driver(headless), 1);

#if defined(__arm__) || defined(__aarch64__)
  fullscreen = 1;	/* the console and the Pi: the whole display, whatever its mode */
#else
  fullscreen = plat_target.vout_fullscreen || ab_opts.fullscreen;	/* -fullscreen: the launcher's rule */
#endif
  ret = plat_ab_init("PCSX-ReARMed " REV, 1280, 720, fullscreen, g_opts & OPT_VSYNC);
  if (ret != 0)
    exit(1);
  if (ab_headless_starts_hidden(headless) && plat_ab_window != NULL)
    SDL_HideWindow(plat_ab_window);
  /* EMU-15: the battery icon (PSCLASSIC only) and the hud_msg/FPS/CPU/SPU-channel notices (every
   * platform) - see ab_hud_draw() above. Registered unconditionally: only one HUD callback exists. */
  plat_ab_set_hud_cb(ab_hud_draw);
  fprintf(stdout, "Audio driver: %s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");
  plat_target.vout_fullscreen = fullscreen_old = plat_ab_is_fullscreen();

  // enough for the largest frame plugin_lib lets through: 2x-enhanced, or scaled by the smoothing
  shadow_fb = calloc(PL_VOUT_MAX_W * PL_VOUT_MAX_H, 2);
  menubg_img = calloc(PL_VOUT_MAX_W * PL_VOUT_MAX_H, 2);
  if (shadow_fb == NULL || menubg_img == NULL) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  resize_cb(plat_ab_win_w, plat_ab_win_h);
  in_menu = 1;

  in_sdl2_init(&in_sdl2_platform_data, sdl_event_handler);
  in_sdl2gc_init(&in_sdl2gc_platform_data, controller_db_files, pads_changed);
  in_probe();

  pl_rearmed_cbs.only_16bpp = 1;
  pl_rearmed_cbs.pl_get_layer_pos = get_layer_pos;
  plat_target.hwfilters = ab_filter_names;
  plugin_update();
  ab_debug_start();	/* AB_DEBUG_PORT: the test driver, after the input drivers it pushes keys to */
}

void plat_finish(void)
{
  free(shadow_fb);
  shadow_fb = NULL;
  free(menubg_img);
  menubg_img = NULL;
  free(menu_fb);
  menu_fb = NULL;
  plat_ab_finish();
  SDL_Quit();
}

/* the menu's "Fullscreen mode" and the F11 action both just flip plat_target.vout_fullscreen */
static void check_fullscreen(void)
{
#if defined(__arm__) || defined(__aarch64__)
  /* the console and the Pi have no desktop to leave fullscreen for: a "plat_target.vout_fullscreen = 0"
   * in a game's pcsx.cfg (every pcsx-ab-era cfg has one) used to drop the window to 1280x720 and bring
   * the mouse pointer back */
  plat_target.vout_fullscreen = 1;
#endif
  if (ab_opts.fullscreen)
    plat_target.vout_fullscreen = 1;	/* started with -fullscreen: a cfg's vout_fullscreen = 0 does not undo it */
  if (plat_target.vout_fullscreen != fullscreen_old) {
    plat_ab_set_fullscreen(plat_target.vout_fullscreen);
    plat_target.vout_fullscreen = fullscreen_old = plat_ab_is_fullscreen();
    plugin_update();
  }
}

void plat_gvideo_open(int is_pal)
{
}

/* plugin_lib worked out g_layer_* for this size against g_menuscreen_w/h before calling; the GPU draws
 * into shadow_fb from here on (pl_plat_blit stays NULL: plugin_lib converts and blits by itself) */
void *plat_gvideo_set_mode(int *w, int *h, int *bpp)
{
  psx_w = *w;
  psx_h = *h;
  *bpp = 16;
  memset(shadow_fb, 0, psx_w * psx_h * 2);
  return shadow_fb;
}

void *plat_gvideo_flip(void)
{
  SDL_Rect dst = { g_layer_x, g_layer_y, g_layer_w, g_layer_h };

  check_fullscreen();
  update_pipeline();
  plat_ab_present(shadow_fb, psx_w, psx_h, psx_w, &dst);
  return shadow_fb;
}

void plat_gvideo_close(void)
{
}

void plat_video_menu_enter(int is_rom_loaded)
{
  int d;

  in_menu = 1;
  /* the last frame is the menu's background; pl_vout_buf points at it while the menu is up */
  memcpy(menubg_img, shadow_fb, psx_w * psx_h * 2);
  pl_vout_buf = menubg_img;

  for (d = 0; d < IN_MAX_DEVS; d++)
    in_set_config_int(d, IN_CFG_ANALOG_MAP_ULDR, 1);
}

void plat_video_menu_begin(void)
{
  check_fullscreen();
  g_menuscreen_ptr = menu_fb;
}

void plat_video_menu_end(void)
{
  plat_ab_present(menu_fb, g_menuscreen_w, g_menuscreen_h, g_menuscreen_pp, NULL);
  g_menuscreen_ptr = NULL;
}

void plat_video_menu_leave(void)
{
  int d;

  in_menu = 0;
  check_fullscreen();
  pl_update_layer_size(psx_w, psx_h, g_menuscreen_w, g_menuscreen_h);
  plat_ab_clear();

  for (d = 0; d < IN_MAX_DEVS; d++)
    in_set_config_int(d, IN_CFG_ANALOG_MAP_ULDR, 0);
}

void *plat_prepare_screenshot(int *w, int *h, int *bpp)
{
  *w = psx_w;
  *h = psx_h;
  *bpp = 16;
  return shadow_fb;
}

/* pad.c's values, as libretro.c maps them: high is the strong motor's 0..255, low the weak one's on/off;
 * called only on a change, so the rumble runs until the next call turns it down */
void plat_trigger_vibrate(int pad, int low, int high)
{
  if (!in_enable_vibration)
    return;
  /* C11, round 3 (Marcus's review): "pad" here is the PS1 port (0-based); the SDL player argument
   * in_sdl2gc_rumble() wants is 1-based acceptance order, same as in_adev[]'s in pads_changed() above -
   * ab_pad_order_state[pad] + 1, not pad + 1, or a swapped setup would rumble the wrong physical pad. */
  in_sdl2gc_rumble(ab_pad_order_state[pad] + 1, low ? 0xffff : 0, high << 8, 5000);
}

void plat_minimize(void)
{
  SDL_MinimizeWindow(plat_ab_window);
}

// vim:shiftwidth=2:expandtab
