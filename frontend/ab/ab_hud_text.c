/*
 * See ab_hud_text.h.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <string.h>

#include "ab_hud_text.h"
#include "../libpicofe/fonts.h"

int ab_hud_text_width(const char *text)
{
	int len;

	if (text == NULL)
		return 0;
	len = (int)strlen(text);
	if (len > AB_HUD_TEXT_MAXLEN)
		len = AB_HUD_TEXT_MAXLEN;
	return len * AB_HUD_GLYPH_W;
}

void ab_hud_text_render_argb(uint32_t *px, int tex_w, int tex_h, const char *text)
{
	int i, l, len;

	if (px == NULL || text == NULL || tex_h < AB_HUD_GLYPH_H)
		return;

	len = (int)strlen(text);
	if (len > AB_HUD_TEXT_MAXLEN)
		len = AB_HUD_TEXT_MAXLEN;

	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)text[i];
		int gx = i * AB_HUD_GLYPH_W;

		if (gx + AB_HUD_GLYPH_W > tex_w)
			break;
		if (c == ' ' || c >= 128)
			continue;

		for (l = 0; l < AB_HUD_GLYPH_H; l++) {
			unsigned char fd = fontdata8x8[c * 8 + l];
			unsigned char bit;
			int col;

			for (col = 0, bit = 0x80; bit != 0; bit >>= 1, col++)
				if (fd & bit)
					px[l * tex_w + gx + col] = 0xffffffffu;
		}
	}
}

uint32_t ab_hud_rgb565_to_argb(unsigned short rgb565)
{
	unsigned r5 = (rgb565 >> 11) & 0x1f;
	unsigned g6 = (rgb565 >> 5) & 0x3f;
	unsigned b5 = rgb565 & 0x1f;
	unsigned r8 = (r5 * 255 + 15) / 31;
	unsigned g8 = (g6 * 255 + 31) / 63;
	unsigned b8 = (b5 * 255 + 15) / 31;

	return 0xff000000u | (r8 << 16) | (g8 << 8) | b8;
}
