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
#include "ab/ab_buttons.h"
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
static void *menu_fb;			/* what the menu draws into, RGB565, the window's size (4:3: the safe rect's) */
/* A 4:3 output (ab_layout43(): the tube's 720x480 and the VGA modes): the menu draws in square pixels on a canvas
 * the safe rect's size - on the tube (ab_crt43()) a 4:3 one the safe rect's height, which crt_compose() stretches
 * 9:8 into the safe rect of crt_fb, on VGA the safe rect itself, copied 1:1 - and the margin around it in crt_fb
 * (the output's size) is the picture's edges mirrored and dimmed (the launcher's CRT margin,
 * Renderer::mirrorMargin) */
static unsigned short *crt_fb;
static int out43_w, out43_h;		/* the 4:3 output's size, 0 on a wide one */
static int crt_mx, crt_my;		/* the margin's width and height in output pixels */
static void *menubg_img;		/* the last frame, the menu's background */
static int in_menu;
static int fullscreen_old;

static void quit_cb(void)
{
  emu_core_ask_exit();
}

/* a quit (the window's close, or SIGTERM - SDL turns it into SDL_QUIT, quit_cb above) while a menu waits for
 * a key: the menus unwind as if Back were pressed, and main()'s loop ends the run the normal way (EMU-18).
 * The power daemon's power-off and the overheat stop too: the menus unwind, the game goes on, and its next
 * frame tick (ab_frame_tick) leaves the way it does in the game - leave(), the memory card, the clean frames,
 * the resume point. Only with a game running: without one there is no frame tick to leave through */
static int menu_reset;	/* the Reset key went down in a menu while a game runs: the run is ending */

static int menu_quit_check(void)
{
  return g_emu_want_quit || menu_reset ||
    ((ab_console_power_off_requested || ab_console_overheated) && ready_to_go);
}

/* the console's Reset button (or F10 - whatever pcsx.cfg binds to "RESET button") is an emulator action, which
 * a menu's key wait does not read: pressed in a menu it is the same as in the game - the menus unwind, and the
 * first frame tick after them asks for SACTION_AB_RESET (ab_defer_action), so it leaves through leave() */
static void menu_emu_key(int acts)
{
  if ((acts & (1 << SACTION_AB_RESET)) && ready_to_go && !menu_reset) {
    menu_reset = 1;
    ab_defer_action(SACTION_AB_RESET);
  }
}

/* the window's output size changed: the menu's canvas and the layer the frame is scaled into follow */
static void resize_cb(int w, int h)
{
  /* a 4:3 output (width / height <= 1.5, the launcher's OutputMode::is43): the menu's canvas is the safe rect
   * (on the tube in 4:3 square pixels, stretched 9:8 later); only the tube (720x480, CRT 4:3) also gives the
   * game's layer the whole output (pl_crt_out_*) - on VGA the game keeps the player's scaler */
  free(crt_fb);
  crt_fb = NULL;
  pl_crt_out_w = pl_crt_out_h = 0;
  out43_w = out43_h = 0;
  if (w > 0 && h > 0 && w * 2 <= h * 3) {
    crt_fb = calloc(w * h, 2);
    if (crt_fb == NULL) {
      fprintf(stderr, "OOM\n");
      exit(1);
    }
    out43_w = w;
    out43_h = h;
    if (w == 720 && h == 480) {
      pl_crt_out_w = w;
      pl_crt_out_h = h;
    }
    crt_mx = (w * ab_crt_margin + 50) / 100;
    crt_my = (h * ab_crt_margin + 50) / 100;
    h -= 2 * crt_my;
    w = pl_crt_out_w != 0 ? (h * 4 + 1) / 3 : w - 2 * crt_mx;
  }
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

/* a 4:3 output: the HUD keeps inside the CRT margin - its parts are laid out on a screen of square pixels the
 * safe rect's size (hud_area(); on the tube 4:3 the safe rect's height), and ab_hud_put() maps that into the
 * safe rect (on the tube 9:8 wide, on VGA 1:1); on a wide output both pass through */
static int hud_area_w(int ih)
{
  return pl_crt_out_w != 0 ? (ih * 4 + 1) / 3 : out43_w - 2 * crt_mx;
}

static void hud_area(int *sw, int *sh)
{
  if (out43_w == 0)
    return;
  *sh = out43_h - 2 * crt_my;
  *sw = hud_area_w(*sh);
}

static void ab_hud_put(int slot, const SDL_Rect *r)
{
  SDL_Rect o = *r;

  if (out43_w != 0) {
    int iw = out43_w - 2 * crt_mx, aw = hud_area_w(out43_h - 2 * crt_my);
    o.x = crt_mx + r->x * iw / aw;
    o.w = crt_mx + (r->x + r->w) * iw / aw - o.x;
    o.y = crt_my + r->y;
  }
  plat_ab_hud_draw(slot, &o);
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

#define AB_HUD_ARGB_BOLT      0xff36d9e0u  /* the charging bolt: the launcher ab2 icon's cyan... */
#define AB_HUD_ARGB_BOLT_EDGE 0xff0e161eu  /* ...and its dark edge */
#define AB_HUD_MAX_SCALE 6               /* hud_scale()'s ceiling */

/* the icon is built at the screen's pixels (the HUD scale times the 1x design above) and drawn 1:1: the rects
 * look exactly as the 1x texture scaled up did, and the bolt's slanted edges get the screen's resolution */
static Uint32 ab_hud_battery_px[AB_HUD_TEX_W * AB_HUD_MAX_SCALE * AB_HUD_TEX_H * AB_HUD_MAX_SCALE];

typedef struct {
  Uint32 *px;
  int w, h, s;  /* the buffer's size and the scale of the 1x design */
} AbHudIcon;

static void ab_hud_battery_set(const AbHudIcon *ic, int x, int y, Uint32 argb)
{
  if ((unsigned)x >= (unsigned)ic->w || (unsigned)y >= (unsigned)ic->h)
    return;
  ic->px[y * ic->w + x] = argb;
}

/* a rect of the 1x design, at the icon's scale */
static void ab_hud_battery_fill(const AbHudIcon *ic, int x0, int y0, int x1, int y1, Uint32 argb)
{
  int x, y;
  for (y = y0 * ic->s; y < y1 * ic->s; y++)
    for (x = x0 * ic->s; x < x1 * ic->s; x++)
      ab_hud_battery_set(ic, x, y, argb);
}

/* the launcher ab2 icon's bolt (the owner's, 2026-10-05): its points in 2x coordinates of a 58x26 battery */
static const float ab_hud_bolt[6][2] = {
  { 30.0f, 4.5f }, { 18.5f, 14.5f }, { 25.0f, 14.5f }, { 22.5f, 21.5f }, { 34.0f, 11.5f }, { 27.5f, 11.5f }
};

/* squared distance from (x, y) to the segment a-b */
static float ab_hud_seg_dist2(float x, float y, float ax, float ay, float bx, float by)
{
  float dx = bx - ax, dy = by - ay, t = 0.0f, len2 = dx * dx + dy * dy, ex, ey;
  if (len2 > 0.0f) {
    t = ((x - ax) * dx + (y - ay) * dy) / len2;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
  }
  ex = ax + t * dx - x;
  ey = ay + t * dy - y;
  return ex * ex + ey * ey;
}

/* the bolt over the battery's whole shape (body and nub, design x 1..1+bw+nub, y 1..1+bh): cyan inside, a dark
 * edge around it so it reads over the fill and the outline alike */
static void ab_hud_battery_bolt(const AbHudIcon *ic, int bw, int bh)
{
  float pts[6][2], edge = ic->s >= 4 ? ic->s * 0.5f : 1.0f;
  float bx = (float)ic->s, by = (float)ic->s;
  float sx = (float)((bw + AB_HUD_NUB_W) * ic->s) / 58.0f, sy = (float)(bh * ic->s) / 26.0f;
  int i, j, x, y;

  for (i = 0; i < 6; i++) {
    pts[i][0] = bx + ab_hud_bolt[i][0] * sx;
    pts[i][1] = by + ab_hud_bolt[i][1] * sy;
  }
  for (y = 0; y < ic->h; y++) {
    for (x = 0; x < ic->w; x++) {
      float cx = x + 0.5f, cy = y + 0.5f, d2 = 1e9f;
      int inside = 0;
      for (i = 0, j = 5; i < 6; j = i++) {
        float d;
        if ((pts[i][1] > cy) != (pts[j][1] > cy) &&
            cx < (pts[j][0] - pts[i][0]) * (cy - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0])
          inside = !inside;
        d = ab_hud_seg_dist2(cx, cy, pts[j][0], pts[j][1], pts[i][0], pts[i][1]);
        if (d < d2)
          d2 = d;
      }
      if (inside)
        ab_hud_battery_set(ic, x, y, AB_HUD_ARGB_BOLT);
      else if (d2 <= edge * edge)
        ab_hud_battery_set(ic, x, y, AB_HUD_ARGB_BOLT_EDGE);
    }
  }
}

/* fills the icon's buffer for this percent - the same shape plugin_lib.c's old draw_pad_battery_icon drew, one
 * pixel bigger all around for the backing border - and the bolt over it while the pad charges */
static void ab_hud_battery_build(const AbHudIcon *ic, int percent, int charging)
{
  int ox = 1, oy = 1, bw = AB_HUD_ICON_W, bh = AB_HUD_ICON_H;
  int nub_x0, nub_y0, nub_y1, fill_w;
  Uint32 fill_color = percent <= AB_PAD_BATTERY_LOW_PERCENT ? AB_HUD_ARGB_LOW :
                      percent <= 50 ? AB_HUD_ARGB_MED : AB_HUD_ARGB_OK;

  /* solid backing so the outline reads over a bright game frame, then the outline itself */
  ab_hud_battery_fill(ic, ox - 1, oy - 1, ox + bw + AB_HUD_NUB_W + 1, oy + bh + 1, AB_HUD_ARGB_BACKING);
  ab_hud_battery_fill(ic, ox, oy, ox + bw, oy + 1, AB_HUD_ARGB_OUTLINE);
  ab_hud_battery_fill(ic, ox, oy + bh - 1, ox + bw, oy + bh, AB_HUD_ARGB_OUTLINE);
  ab_hud_battery_fill(ic, ox, oy, ox + 1, oy + bh, AB_HUD_ARGB_OUTLINE);
  ab_hud_battery_fill(ic, ox + bw - 1, oy, ox + bw, oy + bh, AB_HUD_ARGB_OUTLINE);

  /* the nub on the right, a third of the body's height, centred */
  nub_x0 = ox + bw;
  nub_y0 = oy + bh / 3;
  nub_y1 = oy + bh - bh / 3;
  ab_hud_battery_fill(ic, nub_x0, nub_y0, nub_x0 + AB_HUD_NUB_W, nub_y1, AB_HUD_ARGB_OUTLINE);

  /* the fill itself, proportional to the percent, inset one pixel inside the outline */
  fill_w = (bw - 4) * percent / 100;
  if (fill_w > 0)
    ab_hud_battery_fill(ic, ox + 2, oy + 2, ox + 2 + fill_w, oy + bh - 2, fill_color);

  if (charging)
    ab_hud_battery_bolt(ic, bw, bh);
}

static void ab_hud_battery_draw(int sw, int sh)
{
  static int icon_percent = -2;  /* not a real percent: forces the first upload */
  static int icon_charging = -1, icon_scale = -1;
  int percent, charging, scale, margin;
  SDL_Rect r;

  if (!ab_pad_battery_visible())
    return;
  percent = ab_pad_battery_percent();
  if (percent < 0)
    return;
  charging = ab_pad_battery_charging();
  scale = hud_scale(sh);

  /* built and uploaded only when what it shows changes - never per frame */
  if (icon_percent != percent || icon_charging != charging || icon_scale != scale) {
    AbHudIcon ic;
    ic.px = ab_hud_battery_px;
    ic.s = scale;
    ic.w = AB_HUD_TEX_W * scale;
    ic.h = AB_HUD_TEX_H * scale;
    memset(ab_hud_battery_px, 0, sizeof(Uint32) * ic.w * ic.h);  /* transparent black */
    ab_hud_battery_build(&ic, percent, charging);
    if (plat_ab_hud_image(HUD_SLOT_BATTERY, ab_hud_battery_px, ic.w, ic.h) != 0)
      return;
    icon_percent = percent;
    icon_charging = charging;
    icon_scale = scale;
  }
  margin = 6 * scale / 2;
  r.w = AB_HUD_TEX_W * scale;
  r.h = AB_HUD_TEX_H * scale;
  r.x = sw - r.w - margin;
  r.y = margin;
  ab_hud_put(HUD_SLOT_BATTERY, &r);
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
  /* the skin's hud_scale (percent, skin.cfg): Red Hat Text's glyphs as large as Selawik's were; the strip
   * (size + size / 3) still fits its slot. Only here, when the line's text changed - never per frame */
  size = (size * ab_ui_skin()->hud_scale + 50) / 100;
  if (size > HUD_LINE_MAX_H * 3 / 4)
    size = HUD_LINE_MAX_H * 3 / 4;
  if (!ab_hud_line_render(px, ab_ui_tr(text), size, scale, &w, &h))
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
    ab_hud_put(HUD_SLOT_MSG, &r);
  }
  if (ab_hud_line_update(&cpu_line, ab_hud_cpu_line(), sh)) {
    r.w = cpu_line.w;
    r.h = cpu_line.h;
    r.x = sw - r.w - margin;
    r.y = sh - r.h - margin;
    ab_hud_put(HUD_SLOT_CPU, &r);
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
  ab_hud_put(HUD_SLOT_CHANS, &r);
}

/* the HUD callback: the battery icon (PSCLASSIC only, as it always was) plus the notices (every platform) */
static void ab_hud_draw(int sw, int sh)
{
  hud_area(&sw, &sh);
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
  if (crt && ab_crt43()) {
    filter = AB_FILTER_LINEAR;	/* a real tube: no CRT filter, whatever the cfg or the launcher passed */
    crt = 0;
  }
  if (console && soft_filter > SOFT_FILTER_EAGLE2X)
    soft_filter = SOFT_FILTER_NONE;	/* hq2x/hq3x are not offered on the console (a cfg from before) */
  smooth = ab_smooth_shader(soft_filter);
  if (console && crt)
    smooth = NULL;
  plat_ab_set_pipeline(smooth, ab_filter_shader(filter));
  level = crt || ab_crt43() ? 0 : scanlines < 0 ? 0 : scanlines > 3 ? 3 : scanlines;
  plat_ab_set_scanlines(pattern[level][0], pattern[level][1], (100 - scanline_level) * 255 / 100);
}

/* the output mode (ab_config.h) */
int ab_output_mode;
int ab_crt_margin = 5;

int ab_crt43(void)
{
  return pl_crt_out_w != 0;	/* resize_cb(): the output is 720x480 */
}

int ab_layout43(void)
{
  return out43_w != 0;	/* resize_cb(): any 4:3 output, the tube's included */
}

int ab_output_mode_parse(const char *s)
{
  int w, h;
  char end;

  if (s == NULL)
    return AB_OUTPUT_AUTO;
  if (strcmp(s, "720") == 0)
    return AB_OUTPUT_720;
  if (strcmp(s, "1080") == 0)
    return AB_OUTPUT_1080;
  if (sscanf(s, "%dx%d%c", &w, &h, &end) == 2 && w > 0 && h > 0 && w < 32768 && h < 65536)
    return AB_OUTPUT_MODE(w, h);
  return AB_OUTPUT_AUTO;
}

int ab_output_mode_available(int mode)
{
  return mode == AB_OUTPUT_AUTO || plat_ab_has_mode(AB_OUTPUT_W(mode), AB_OUTPUT_H(mode));
}

/* the launcher's token for a mode (OutputMode::token): auto, 720, 1080, <w>x<h> */
static void ab_output_mode_token(int mode, char *buf, int size)
{
  if (mode == AB_OUTPUT_AUTO)
    snprintf(buf, size, "auto");
  else if (mode == AB_OUTPUT_720)
    snprintf(buf, size, "720");
  else if (mode == AB_OUTPUT_1080)
    snprintf(buf, size, "1080");
  else
    snprintf(buf, size, "%dx%d", AB_OUTPUT_W(mode), AB_OUTPUT_H(mode));
}

void ab_output_mode_name(int mode, char *buf, int size)
{
  int w = AB_OUTPUT_W(mode), h = AB_OUTPUT_H(mode);

  if (mode == AB_OUTPUT_AUTO)
    snprintf(buf, size, "Auto");
  else if (w == 720 && h == 480)
    snprintf(buf, size, "CRT 4:3");
  else if (w * 9 == h * 16)
    snprintf(buf, size, "%dp", h);
  else
    snprintf(buf, size, "%dx%d", w, h);
}

/* whether mode a is listed before mode b */
static int ab_output_mode_before(int a, int b)
{
  int wa = AB_OUTPUT_W(a), ha = AB_OUTPUT_H(a), wb = AB_OUTPUT_W(b), hb = AB_OUTPUT_H(b);
  int tva = wa * 9 == ha * 16, tvb = wb * 9 == hb * 16;

  if (tva != tvb)
    return tva;
  return wa * ha != wb * hb ? wa * ha < wb * hb : wa < wb;
}

int ab_output_modes(int *modes, int max)
{
  SDL_DisplayMode m;
  int di, n = 0, i, j, k, count;

  if (max < 1)
    return 0;
  modes[n++] = AB_OUTPUT_AUTO;
  if (plat_ab_window == NULL || (di = SDL_GetWindowDisplayIndex(plat_ab_window)) < 0)
    return n;
  count = SDL_GetNumDisplayModes(di);
  for (i = 0; i < count && n < max; i++) {
    int mode;
    if (SDL_GetDisplayMode(di, i, &m) != 0 || (m.refresh_rate != 0 && m.refresh_rate < 50))
      continue;	/* a 4K TV's 24/30 Hz modes: a game would stutter in them */
    mode = AB_OUTPUT_MODE(m.w, m.h);
    for (j = 1; j < n && modes[j] != mode; j++)
      ;
    if (j < n)
      continue;
    /* the TV modes (16:9) first, then the rest (VESA), each group from the smallest - as the launcher lists them */
    for (j = 1; j < n && ab_output_mode_before(modes[j], mode); j++)
      ;
    for (k = n; k > j; k--)
      modes[k] = modes[k - 1];
    modes[j] = mode;
    n++;
  }
  return n;
}

int ab_output_mode_apply(int mode, int tell_launcher)
{
  const char *dir = getenv("AB_RUNTIME_DIR");
  int ret;

  if (!ab_output_mode_available(mode))
    mode = AB_OUTPUT_AUTO;
  ret = plat_ab_set_output_mode(AB_OUTPUT_W(mode), AB_OUTPUT_H(mode));
  if (ret != 0)
    return ret;
  ab_output_mode = mode;
  /* the launcher takes the player's choice from its runtime directory (RAM) when the game ends */
  if (tell_launcher && dir != NULL && *dir) {
    char path[512], token[32];
    FILE *f;
    snprintf(path, sizeof(path), "%s/outputmode", dir);
    ab_output_mode_token(mode, token, sizeof(token));
    f = fopen(path, "w");
    if (f != NULL) {
      fprintf(f, "%s\n", token);
      fclose(f);
    }
  }
  return 0;
}

void plat_init(void)
{
  int fullscreen, ret, headless;

  {
    /* the CRT margin (ab_config.h) before the first resize_cb(), which lays the CRT's menu out by it */
    const char *m = getenv("AB_CRT_MARGIN");
    char *end;
    long v = m != NULL ? strtol(m, &end, 10) : -1;
    if (m != NULL && *m != 0 && *end == 0 && v >= 0 && v <= 20)
      ab_crt_margin = (int)v;
  }
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
  /* the launcher's output mode (AB_OUTPUT_MODE, abfeatures' "outputmode") */
  ab_output_mode = ab_output_mode_parse(getenv("AB_OUTPUT_MODE"));
  if (ab_output_mode != AB_OUTPUT_AUTO)
    ab_output_mode_apply(ab_output_mode, 0);

  in_sdl2_init(&in_sdl2_platform_data, sdl_event_handler);
  in_sdl2gc_init(&in_sdl2gc_platform_data, controller_db_files, pads_changed);
  in_probe();
  in_set_menu_quit_check(menu_quit_check);
  in_set_menu_emu_key(menu_emu_key);

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
  free(crt_fb);
  crt_fb = NULL;
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

/* RGB565 a..b at f/256 */
static unsigned short crt_lerp(unsigned short a, unsigned short b, int f)
{
  int r = (a >> 11) + ((((b >> 11) - (a >> 11)) * f) >> 8);
  int g = ((a >> 5) & 63) + (((((b >> 5) & 63) - ((a >> 5) & 63)) * f) >> 8);
  int bl = (a & 31) + ((((b & 31) - (a & 31)) * f) >> 8);
  return (unsigned short)(r << 11 | g << 5 | bl);
}

/* a 4:3 output from the menu's canvas: the canvas stretched (bilinear across, its rows as they are) into the
 * safe rect - on VGA the same width, so a plain copy - then the margin, each side the rows/columns next to it
 * mirrored, at half brightness */
#define CRT_COLS_MAX 4096
static void crt_compose(void)
{
  static int col_src[CRT_COLS_MAX], col_f[CRT_COLS_MAX], cols = -1, cols_w = -1;
  const unsigned short *src = menu_fb;
  int ow = out43_w, oh = out43_h, iw = ow - 2 * crt_mx, ih = oh - 2 * crt_my;
  int x, y, sw = g_menuscreen_w;

  if (iw > CRT_COLS_MAX)
    iw = CRT_COLS_MAX;

  if (cols != iw || cols_w != sw) {
    for (x = 0; x < iw; x++) {
      int fx = (int)(((2LL * x + 1) * sw * 256) / (2 * iw)) - 128;	/* the column's centre, 24.8 */
      if (fx < 0)
        fx = 0;
      col_src[x] = fx >> 8;
      col_f[x] = col_src[x] + 1 < sw ? fx & 255 : 0;
    }
    cols = iw;
    cols_w = sw;
  }
  for (y = 0; y < ih && y < g_menuscreen_h; y++) {
    const unsigned short *s = src + (size_t)y * g_menuscreen_pp;
    unsigned short *d = crt_fb + (size_t)(crt_my + y) * ow;
    for (x = 0; x < iw; x++) {
      int i = col_src[x];
      d[crt_mx + x] = col_f[x] ? crt_lerp(s[i], s[i + 1], col_f[x]) : s[i];
    }
    for (x = 0; x < crt_mx; x++) {
      d[crt_mx - 1 - x] = (d[crt_mx + x] >> 1) & 0x7bef;
      d[crt_mx + iw + x] = (d[crt_mx + iw - 1 - x] >> 1) & 0x7bef;
    }
  }
  for (y = 0; y < crt_my; y++) {
    const unsigned short *t = crt_fb + (size_t)(crt_my + y) * ow, *b = crt_fb + (size_t)(crt_my + ih - 1 - y) * ow;
    unsigned short *dt = crt_fb + (size_t)(crt_my - 1 - y) * ow, *db = crt_fb + (size_t)(crt_my + ih + y) * ow;
    for (x = 0; x < ow; x++) {
      dt[x] = (t[x] >> 1) & 0x7bef;
      db[x] = (b[x] >> 1) & 0x7bef;
    }
  }
}

void plat_video_menu_end(void)
{
  if (crt_fb != NULL) {
    crt_compose();
    plat_ab_present(crt_fb, out43_w, out43_h, out43_w, NULL);
  } else
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
