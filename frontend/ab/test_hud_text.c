/*
 * A standalone smoke test for ab_hud_text.c's bitmap-font rendering (EMU-15 part 2): the same fontdata8x8
 * glyphs frontend/plugin_lib.c's old hud_print() drew straight into the PSX-resolution frame (where the
 * scanline overlay then covered them) are rendered here into an ARGB8888 buffer instead, for
 * plat_sdl2.c's HUD overlay to present after the scanlines. No SDL needed - this only exercises the pure
 * pixel logic. Not part of the emulator's CMake build, same as ab_pad_battery.c's test.
 *
 *   gcc -o test_hud_text ab_hud_text.c ../libpicofe/fonts.c test_hud_text.c && ./test_hud_text
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <string.h>

#include "ab_hud_text.h"
#include "../libpicofe/fonts.h"

static int failures;

static void expect(int cond, const char *what)
{
	if (!cond) {
		printf("FAIL: %s\n", what);
		failures++;
	} else {
		printf("ok:   %s\n", what);
	}
}

/* true if the tex_w x AB_HUD_GLYPH_H buffer at column-offset gx reproduces fontdata8x8[c] bit for bit */
static int glyph_matches(const uint32_t *px, int tex_w, int gx, unsigned char c)
{
	int l, col;

	for (l = 0; l < AB_HUD_GLYPH_H; l++) {
		unsigned char fd = fontdata8x8[c * 8 + l];
		unsigned char bit;

		for (col = 0, bit = 0x80; bit != 0; bit >>= 1, col++) {
			int want = (fd & bit) ? 1 : 0;
			int got = px[l * tex_w + gx + col] == 0xffffffffu ? 1 : 0;
			if (want != got)
				return 0;
		}
	}
	return 1;
}

int main(void)
{
	uint32_t px[AB_HUD_GLYPH_W * AB_HUD_GLYPH_H * 4];
	int w;

	expect(ab_hud_text_width(NULL) == 0, "NULL text: zero width");
	expect(ab_hud_text_width("") == 0, "empty text: zero width");
	expect(ab_hud_text_width("AB") == 2 * AB_HUD_GLYPH_W, "two glyphs: 2 * AB_HUD_GLYPH_W wide");

	{
		char long_text[AB_HUD_TEXT_MAXLEN + 10];
		memset(long_text, 'X', sizeof(long_text) - 1);
		long_text[sizeof(long_text) - 1] = 0;
		expect(ab_hud_text_width(long_text) == AB_HUD_TEXT_MAXLEN * AB_HUD_GLYPH_W,
			"over-long text: truncated to AB_HUD_TEXT_MAXLEN glyphs");
	}

	/* "A" alone: the buffer must match fontdata8x8['A'] bit for bit - the exact glyph the frame used to
	 * get, drawn into a texture instead */
	memset(px, 0, sizeof(px));
	w = ab_hud_text_width("A");
	ab_hud_text_render_argb(px, w, AB_HUD_GLYPH_H, "A");
	expect(glyph_matches(px, w, 0, (unsigned char)'A'), "'A': pixel buffer matches fontdata8x8 bit for bit");

	/* a space leaves its whole cell untouched (transparent, if the caller started from a zeroed buffer -
	 * same as the original skipping it) */
	memset(px, 0xff, sizeof(px));
	w = ab_hud_text_width(" ");
	memset(px, 0, (size_t)w * AB_HUD_GLYPH_H * 4);
	ab_hud_text_render_argb(px, w, AB_HUD_GLYPH_H, " ");
	{
		int i, allzero = 1;
		for (i = 0; i < w * AB_HUD_GLYPH_H; i++)
			if (px[i] != 0) { allzero = 0; break; }
		expect(allzero, "space: its cell stays untouched/transparent");
	}

	/* two glyphs side by side: each is an exact, non-overlapping copy at its own 8px offset */
	memset(px, 0, sizeof(px));
	w = ab_hud_text_width("II");
	ab_hud_text_render_argb(px, w, AB_HUD_GLYPH_H, "II");
	expect(glyph_matches(px, w, 0, (unsigned char)'I'), "'II': the first glyph matches fontdata8x8['I']");
	expect(glyph_matches(px, w, AB_HUD_GLYPH_W, (unsigned char)'I'),
		"'II': the second glyph is an exact copy at its own 8px offset");

	/* rgb565->argb: pure white and pure black round-trip exactly; a mid green comes out green, opaque */
	expect(ab_hud_rgb565_to_argb(0xffff) == 0xffffffffu, "rgb565 white -> argb white, opaque");
	expect(ab_hud_rgb565_to_argb(0x0000) == 0xff000000u, "rgb565 black -> argb black, opaque");
	{
		uint32_t argb = ab_hud_rgb565_to_argb(0x07e0); /* pure green in 565 */
		expect((argb >> 24) == 0xff, "rgb565 green: opaque");
		expect(((argb >> 16) & 0xff) == 0, "rgb565 green: no red");
		expect(((argb >> 8) & 0xff) == 0xff, "rgb565 green: full green");
		expect((argb & 0xff) == 0, "rgb565 green: no blue");
	}

	printf(failures == 0 ? "\nAll tests passed.\n" : "\n%d test(s) FAILED.\n", failures);
	return failures == 0 ? 0 : 1;
}
