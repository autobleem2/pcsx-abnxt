/*
 * The render pipeline's passes (docs/render-pipeline-plan.md): which shader each filter and each smoothing
 * is, handed to libpicofe's plat_autobleem by pointer. The sources are the .glsl files in frontend/ab/shaders/.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PCSXAB_AB_SHADERS_H
#define PCSXAB_AB_SHADERS_H

struct plat_ab_shader;

/* plat_target.hwfilter indexes this (NULL-terminated); 0/1 are Nearest/Linear, what the launcher's -filter
 * 0/1 always meant, and 2 is sharp-bilinear, which took the old "Sharp"'s place */
enum {
	AB_FILTER_NEAREST,
	AB_FILTER_LINEAR,
	AB_FILTER_SHARP,
	AB_FILTER_SHARP_SIMPLE,
	AB_FILTER_QUILEZ,
	AB_FILTER_CRT_FAST,
	AB_FILTER_CRT_PI,
	AB_FILTER_COUNT
};
extern const char *ab_filter_names[AB_FILTER_COUNT + 1];

const struct plat_ab_shader *ab_filter_shader(int filter);
/* a CRT filter draws its own scanlines: ours are off with it, and on the PSC so is the smoothing (inline:
 * the menu, built on every platform, asks too) */
static inline int ab_filter_is_crt(int filter)
{
	return filter == AB_FILTER_CRT_FAST || filter == AB_FILTER_CRT_PI;
}
/* the GPU pass for a soft_filter (SOFT_FILTER_*), NULL for none or one the CPU does (hq2x/hq3x) */
const struct plat_ab_shader *ab_smooth_shader(int soft_filter);

#endif
