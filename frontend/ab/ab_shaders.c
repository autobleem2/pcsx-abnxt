/*
 * The render pipeline's passes - see ab_shaders.h.
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

#include "../libpicofe/plat_autobleem.h"
#include "../menu.h"
#include "ab_console.h"
#include "ab_shaders.h"
#include "ab_shaders_src.h"

/* one read per pixel: the input's own filter makes it nearest or bilinear */
static const char copy_src[] =
	"#if defined(VERTEX)\n"
	"attribute vec4 VertexCoord;\n"
	"attribute vec4 TexCoord;\n"
	"varying vec2 uv;\n"
	"uniform mat4 MVPMatrix;\n"
	"void main() { gl_Position = MVPMatrix * VertexCoord; uv = TexCoord.xy; }\n"
	"#elif defined(FRAGMENT)\n"
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"varying vec2 uv;\n"
	"uniform sampler2D Texture;\n"
	"void main() { gl_FragColor = texture2D(Texture, uv); }\n"
	"#endif\n";

const char *ab_filter_names[AB_FILTER_COUNT + 1] = {
	"Nearest", "Linear", "Sharp", "Sharp (simple)", "Quilez", "CRT (fast)", "CRT-Pi", "CRT (raster)", NULL
};

const char *ab_crt_mask_names[AB_CRT_MASK_COUNT + 1] = { "Aperture", "Slot", "Delta", "Off", NULL };

/* CRT (raster)'s weight (crt-raster.glsl's header): 0 the full one, 1 light (CRT_FAST_GAMMA), 2 lite
 * (CRT_LITE). The same menu choice everywhere, the build by the machine (the owner, 2026-10-12): lite on
 * the console, whose PowerVR fell under 40 fps in Tekken 3 at 720p with the full one, full elsewhere (the
 * Pi's look, approved). AB_CRT_RASTER=full|light|lite in the environment overrides it. */
#define RASTER_VARIANTS 3

static int raster_variant(void)
{
	static int variant = -1;
	if (variant < 0) {
		const char *v = getenv("AB_CRT_RASTER");
		variant = ab_console_present() ? 2 : 0;
		if (v != NULL && strcmp(v, "full") == 0)
			variant = 0;
		else if (v != NULL && strcmp(v, "light") == 0)
			variant = 1;
		else if (v != NULL && strcmp(v, "lite") == 0)
			variant = 2;
		if (variant < 0 || variant >= RASTER_VARIANTS)
			variant = 0;
	}
	return variant;
}

/* GLES 3: the mask is a texture (CRT_MASK_TEXTURE, crt_mask_pattern below) - one read a pixel, the same for
 * every mask; GLES 2 cannot repeat a texture 3 or 6 texels wide, so it works the mask out per pixel */
static int raster_mask_texture(void)
{
	return plat_ab_gles_version() >= 3;
}

int ab_crt_mask_offered(int mask)
{
	/* the lite build's own mask maths has no slot mask: its dark rows cost the console's GPU the frames it had
	 * left; as a texture it costs what the others do */
	return mask >= 0 && mask < AB_CRT_MASK_COUNT &&
		!(mask == AB_CRT_MASK_SLOT && raster_variant() == 2 && !raster_mask_texture());
}

/* crt-raster.glsl once per mask (its CRT_MASK), made the first time it is asked for: a program per mask,
 * so the mask costs no branch per pixel */
static char raster_src[AB_CRT_MASK_COUNT][sizeof(ab_glsl_crt_raster) + 96];
static struct plat_ab_shader raster[AB_CRT_MASK_COUNT];

#define MASK_DARK 0.70f	/* crt-raster.glsl's */

/* CRT (raster)'s mask as a pattern of 2p x 16 screen pixels, which every mask repeats in (p the triad's width,
 * the slot mask's two staggered columns of triads, delta's rows of dots and the slot's 4 or 8 rows): the
 * shader's CRT_FAST_GAMMA mask maths for pixel (x, y), y counted from the bottom as gl_FragCoord is, stored
 * halved (the lit stripes go above 1) */
static int crt_mask_pattern(const struct plat_ab_shader *sh, int out_w, int out_h, unsigned char *rgb, int *pw,
	int *ph)
{
	int mask = (int)(sh - raster);
	int p = out_h >= 1600 ? 6 : out_h >= 900 ? 3 : 2;
	int w = 2 * p, h = 16, x, y, c;

	(void)out_w;
	if (mask < 0 || mask >= AB_CRT_MASK_COUNT || mask == AB_CRT_MASK_OFF)
		return 0;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			float m[3];
			int xx = x;
			if (mask == AB_CRT_MASK_DELTA) {
				/* each row of dots a part of a triad over from the one above */
				int g = p / 3 > 1 ? p / 3 : 1;
				xx += ((y / g) & 1) * (p / 2);
			}
			if (p == 2) {
				/* two pixels: green, then red and blue */
				float lit = 2.0f - MASK_DARK;
				m[0] = m[2] = (xx & 1) ? lit : MASK_DARK;
				m[1] = (xx & 1) ? MASK_DARK : lit;
			} else {
				float lit = 3.0f - 2.0f * MASK_DARK;
				m[0] = m[1] = m[2] = MASK_DARK;
				m[(xx % p) * 3 / p] = lit;
			}
			if (mask == AB_CRT_MASK_SLOT) {
				/* each triad broken by a dark row every slot, the next triad's half a slot lower */
				int rows = p == 6 ? 2 : 1, slot = 4 * rows;
				int odd = (x / p) & 1;
				float k = (float)slot / (slot - rows + rows * MASK_DARK);
				int dark = (y + odd * slot / 2) % slot < rows;
				for (c = 0; c < 3; c++)
					m[c] = (dark ? MASK_DARK : m[c]) * k;
			}
			for (c = 0; c < 3; c++) {
				float v = m[c] * 0.5f * 255.0f + 0.5f;
				rgb[(y * w + x) * 3 + c] = (unsigned char)(v > 255.0f ? 255.0f : v);
			}
		}
	}
	*pw = w;
	*ph = h;
	return 1;
}

static const struct plat_ab_shader *raster_shader(int mask)
{
	static const char *names[AB_CRT_MASK_COUNT] = {
		"crt-raster (aperture)", "crt-raster (slot)", "crt-raster (delta)", "crt-raster (no mask)"
	};
	static const char *defines[RASTER_VARIANTS] = { "", "#define CRT_FAST_GAMMA\n", "#define CRT_LITE\n" };
	if (!ab_crt_mask_offered(mask))
		mask = AB_CRT_MASK_APERTURE;	/* a game saved with Slot on another machine: the console's default */
	if (raster[mask].src == NULL) {
		int tex = raster_mask_texture() && mask != AB_CRT_MASK_OFF;
		snprintf(raster_src[mask], sizeof(raster_src[mask]), "#define CRT_MASK %d\n%s%s%s", mask,
			tex ? "#define CRT_MASK_TEXTURE\n" : "", defines[raster_variant()], ab_glsl_crt_raster);
		raster[mask].name = names[mask];
		raster[mask].src = raster_src[mask];
		raster[mask].scale = 0;
		raster[mask].linear = 1;
		raster[mask].aux = tex ? crt_mask_pattern : NULL;
	}
	return &raster[mask];
}

/* name, source, scale (0: onto the screen), samples its input bilinearly; CRT (raster) is raster_shader's */
static const struct plat_ab_shader filters[AB_FILTER_CRT_RASTER] = {
	{ "nearest",               copy_src,                     0, 0 },
	{ "linear",                copy_src,                     0, 1 },
	{ "sharp-bilinear",        ab_glsl_sharp_bilinear,        0, 1 },
	{ "sharp-bilinear-simple", ab_glsl_sharp_bilinear_simple, 0, 1 },
	{ "quilez",                ab_glsl_quilez,                0, 1 },
	{ "zfast_crt",             ab_glsl_zfast_crt,             0, 1 },
	{ "crt-pi",                ab_glsl_crt_pi,                0, 1 },
};

static const struct plat_ab_shader smooth_scale2x = { "scale2x", ab_glsl_scale2x, 2, 0 };
static const struct plat_ab_shader smooth_eagle2x = { "eagle2x", ab_glsl_eagle2x, 2, 0 };

const struct plat_ab_shader *ab_filter_shader(int filter, int crt_mask)
{
	if (filter < 0 || filter >= AB_FILTER_COUNT)
		filter = AB_FILTER_LINEAR;
	if (filter == AB_FILTER_CRT_RASTER)
		return raster_shader(crt_mask);
	return &filters[filter];
}

const struct plat_ab_shader *ab_smooth_shader(int soft_filter)
{
	switch (soft_filter) {
	case SOFT_FILTER_SCALE2X: return &smooth_scale2x;
	case SOFT_FILTER_EAGLE2X: return &smooth_eagle2x;
	default: return NULL;
	}
}
