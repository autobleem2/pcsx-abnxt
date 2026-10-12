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
 * (CRT_LITE). The target's default is PCSXAB_CRT_RASTER (CMake: lite on the PSC, whose GPU could not hold
 * 60 fps with the full one); AB_CRT_RASTER=full|light|lite in the environment overrides it. */
#ifndef PCSXAB_CRT_RASTER
#define PCSXAB_CRT_RASTER 0
#endif
#define RASTER_VARIANTS 3

static int raster_variant(void)
{
	static int variant = -1;
	if (variant < 0) {
		const char *v = getenv("AB_CRT_RASTER");
		static char test_v[16]; /* TEST ONLY (PSC fps comparison): /tmp/ab_crt_raster when no environment */
		if (v == NULL) {
			FILE *f = fopen("/tmp/ab_crt_raster", "r");
			if (f != NULL) {
				if (fscanf(f, "%15s", test_v) == 1)
					v = test_v;
				fclose(f);
			}
		}
		if (v != NULL)
			fprintf(stderr, "autobleem: crt-raster build %s\n", v);
		variant = PCSXAB_CRT_RASTER;
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

/* crt-raster.glsl once per mask (its CRT_MASK), made the first time it is asked for: a program per mask,
 * so the mask costs no branch per pixel */
static char raster_src[AB_CRT_MASK_COUNT][sizeof(ab_glsl_crt_raster) + 64];
static struct plat_ab_shader raster[AB_CRT_MASK_COUNT];

static const struct plat_ab_shader *raster_shader(int mask)
{
	static const char *names[AB_CRT_MASK_COUNT] = {
		"crt-raster (aperture)", "crt-raster (slot)", "crt-raster (delta)", "crt-raster (no mask)"
	};
	static const char *defines[RASTER_VARIANTS] = { "", "#define CRT_FAST_GAMMA\n", "#define CRT_LITE\n" };
	if (mask < 0 || mask >= AB_CRT_MASK_COUNT)
		mask = AB_CRT_MASK_APERTURE;
	if (raster[mask].src == NULL) {
		snprintf(raster_src[mask], sizeof(raster_src[mask]), "#define CRT_MASK %d\n%s%s", mask,
			defines[raster_variant()], ab_glsl_crt_raster);
		raster[mask].name = names[mask];
		raster[mask].src = raster_src[mask];
		raster[mask].scale = 0;
		raster[mask].linear = 1;
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
