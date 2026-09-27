/*
 * The render pipeline's passes - see ab_shaders.h.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stddef.h>

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
	"Nearest", "Linear", "Sharp", "Sharp (simple)", "Quilez", "CRT (fast)", "CRT-Pi", NULL
};

/* name, source, scale (0: onto the screen), samples its input bilinearly */
static const struct plat_ab_shader filters[AB_FILTER_COUNT] = {
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

const struct plat_ab_shader *ab_filter_shader(int filter)
{
	if (filter < 0 || filter >= AB_FILTER_COUNT)
		filter = AB_FILTER_LINEAR;
	return &filters[filter];
}

int ab_filter_is_crt(int filter)
{
	return filter == AB_FILTER_CRT_FAST || filter == AB_FILTER_CRT_PI;
}

const struct plat_ab_shader *ab_smooth_shader(int soft_filter)
{
	switch (soft_filter) {
	case SOFT_FILTER_SCALE2X: return &smooth_scale2x;
	case SOFT_FILTER_EAGLE2X: return &smooth_eagle2x;
	default: return NULL;
	}
}
