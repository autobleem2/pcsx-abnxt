/*
 * Pure bitmap-font rendering for the HUD overlay (EMU-15 part 2): turns a string into an opaque-on-
 * transparent ARGB8888 pixel buffer using the emulator's existing 8x8 font (libpicofe/fonts.h's
 * fontdata8x8 - the exact glyphs frontend/plugin_lib.c's old hud_print() drew straight into the PSX-
 * resolution frame), plus the RGB565->ARGB8888 widening the SPU channel bar needs. No SDL here on purpose
 * - frontend/plat_autobleem.c is the only caller that turns this into a texture and presents it after the
 * scanlines; kept SDL-free so it can be tested standalone (test_hud_text.c), the same way ab_pad_battery.c
 * is.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PCSXAB_AB_HUD_TEXT_H
#define PCSXAB_AB_HUD_TEXT_H

#include <stdint.h>

#define AB_HUD_GLYPH_W 8
#define AB_HUD_GLYPH_H 8
/* long enough for every hud_msg the frontend ever formats ("STATE SLOT %d [%s]" and friends run longer
 * than the rest, but nothing needs the whole 64-byte hud_msg buffer on screen at once) */
#define AB_HUD_TEXT_MAXLEN 40

/* the ARGB8888 texture width ab_hud_text_render_argb needs for this text (tex_h is always
 * AB_HUD_GLYPH_H). 0 for NULL or an empty string - callers should skip building/drawing a texture
 * entirely rather than pass that width through. Truncates to AB_HUD_TEXT_MAXLEN characters, same as
 * ab_hud_text_render_argb does. */
int ab_hud_text_width(const char *text);

/* renders text into px (tex_w * tex_h uint32_t, tex_w == ab_hud_text_width(text), tex_h >= AB_HUD_GLYPH_H)
 * with the emulator's 8x8 font: each set glyph bit becomes an opaque white pixel (0xffffffff), everything
 * else - including a space's whole cell - is left exactly as px already was (the caller is expected to
 * start from a zeroed/transparent buffer). Characters at or past 128 (fontdata8x8 only has 128 glyphs) are
 * skipped, same as a space. Does nothing if px or text is NULL. */
void ab_hud_text_render_argb(uint32_t *px, int tex_w, int tex_h, const char *text);

/* widens one RGB565 pixel (as the emulator's core renders and as ab_hud_active_chans hands out) to opaque
 * ARGB8888, the standard bit-replication scale (5/6/5 bits -> 8/8/8) */
uint32_t ab_hud_rgb565_to_argb(unsigned short rgb565);

#endif
