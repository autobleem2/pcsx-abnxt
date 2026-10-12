/*
    crt-raster - AutoBleem's CRT filter: a tube's beam and its phosphor mask drawn at the screen's own
    resolution, not the game's.

    (C) AutoBleem team, 2026

    This work is licensed under the terms of the GNU GPLv2 or later.
    See the COPYING file in the top-level directory.

A tube has no pixels: the beam sweeps one line of the picture at a time, as a soft spot whose height grows
with its brightness, and what lights up is the phosphor's RGB raster - the same on the glass whatever the
game drew. So the two things are drawn separately here:

- the beam, per line of the PlayStation's picture (TextureSize is its own line count - libpicofe's
  plat_ab_set_filter_picture): a Gaussian of the line's colour around its centre, from the two lines the
  pixel lies between. Each line's energy is kept (the Gaussian's area is 1 whatever its width), so the
  picture is as bright as without the filter and only the light is redistributed: a dark line is a thin
  spot with a dark gap beside it, a bright one widens and fills the gap. The Gaussian is widened by the
  pixel's own height (a box's variance, h*h/12), which is what sampling it once per pixel needs to keep
  lines of 4 and 5 pixels (1080p's 4.5) even. Across, the line is read with the hardware's bilinear
  filter, its ramp squeezed between the PlayStation's pixels (SHARPNESS) - two reads a pixel in all.
- the mask, per pixel of the screen (gl_FragCoord): CRT_MASK picks the raster (0 aperture grille,
  1 slot mask, 2 delta, 3 none); its period is 3 screen pixels from 900 lines up, 2 below (720p, where a
  triad does not fit: green and magenta), 6 from 1600 up. The lit and dark levels are chosen so the mask
  averages 1 - the picture keeps its brightness, only white clips. With CRT_MASK_TEXTURE (GLES 3) the host
  draws it once into a pattern the mask repeats in (AuxTexture, AuxSize - ab_shaders.c) and each pixel
  reads it back.

Both in linear light (INPUT_GAMMA / OUTPUT_GAMMA). This pass draws only the picture's own rectangle: the
screen halo's bands are drawn from it averaged 16 x 16 (libpicofe's glow pass), where a 2-3 pixel mask is
gone, so they carry no mask.
*/

#ifndef CRT_MASK
#define CRT_MASK 0
#endif

#define SIGMA_DARK   0.27   /* the beam's half-height (Gaussian sigma) in lines: a black line */
#define SIGMA_BRIGHT 0.42   /* ... and a white one */
#define SHARPNESS    1.6    /* across: 1 = plain bilinear, more = a shorter ramp between pixels */
#define MASK_DARK    0.70   /* a phosphor stripe of another colour; the lit one is set from it */
#define INPUT_GAMMA  2.4
#define OUTPUT_GAMMA 2.2
#define SIGMA_LITE   0.36   /* CRT_LITE: one beam height for every colour, between the dark and the bright one */

/*
Two lighter builds of the same picture for a weak GPU (the PSC's PowerVR fell under 40 fps in Tekken 3 at
720p with the full one), chosen by the host with a define, as CRT_MASK is:
- CRT_FAST_GAMMA: gamma 2.0 - a square in and a square root out instead of nine pow() a pixel - and the
  colour maths in mediump, which that GPU runs at twice the rate (the coordinates stay highp: a 640 pixel
  line needs more than mediump's 10 bits for its fractions).
- CRT_LITE (implies the above): the beam's height no longer follows the colour, so its shape is the same
  for every pixel of the draw: a smooth bump (1 - x^2)^2 of the Gaussian's variance and area 1, whose width
  and scale the vertex shader works out once - no exp() and no inversesqrt() per pixel.
*/
#ifdef CRT_LITE
#define CRT_FAST_GAMMA
#endif

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

#if defined(GL_ES) && defined(CRT_FAST_GAMMA)
#define COLOR mediump vec3
#else
#define COLOR vec3
#endif

uniform vec2 TextureSize;
uniform vec2 OutputSize;
varying vec2 uv;
varying float boxVar;
#ifdef CRT_LITE
varying float bumpInvW;	/* 1 / the bump's half-width, in lines */
varying float bumpScale;	/* its peak, so that its area is 1 */
#endif

#if defined(VERTEX)
uniform mat4 MVPMatrix;
attribute vec4 VertexCoord;
attribute vec4 TexCoord;

void main()
{
	float h = TextureSize.y / OutputSize.y;	/* a screen pixel's height in lines */
	boxVar = h * h / 12.0;
#ifdef CRT_LITE
	/* (1 - (x/w)^2)^2 has the variance w^2/7 and the area 16w/15 */
	float w = sqrt(7.0 * (SIGMA_LITE * SIGMA_LITE + boxVar));
	bumpInvW = 1.0 / w;
	bumpScale = 15.0 / (16.0 * w);
#endif
	uv = TexCoord.xy;
	gl_Position = MVPMatrix * VertexCoord;
}
#elif defined(FRAGMENT)

uniform sampler2D Texture;

#ifdef CRT_LITE
/* a line's light at dy lines from its centre: the bump, the same for every colour */
float beam(float dy, COLOR c)
{
	float b = 1.0 - min(dy * dy * bumpInvW * bumpInvW, 1.0);
	return b * b * bumpScale;
}
#else
/* a line's light at dy lines from its centre: a Gaussian of area 1, wider for a brighter colour c (as drawn) */
float beam(float dy, COLOR c)
{
	float s = mix(SIGMA_DARK, SIGMA_BRIGHT, max(c.r, max(c.g, c.b)));
	float v = s * s + boxVar;
	return exp(-dy * dy / (2.0 * v)) * inversesqrt(6.2831853 * v);
}
#endif

#ifdef CRT_FAST_GAMMA
#define TO_LINEAR(c) ((c) * (c))
#define TO_SCREEN(c) sqrt(c)
#else
#define TO_LINEAR(c) pow(c, vec3(INPUT_GAMMA))
#define TO_SCREEN(c) pow(c, vec3(1.0 / OUTPUT_GAMMA))
#endif

#ifdef CRT_MASK_TEXTURE
/* GLES 3: the host drew the mask below (CRT_FAST_GAMMA's) into a small pattern the mask repeats in, stored
   halved, repeated over the screen by the texture unit - one read a pixel, every mask the same cost */
uniform sampler2D AuxTexture;
uniform vec2 AuxSize;	/* the pattern's place stays highp: a half-float's step at 1920 is 1.5 of its texels */

COLOR mask(vec2 pos)
{
	return texture2D(AuxTexture, pos / AuxSize).rgb * 2.0;
}
#elif defined(CRT_FAST_GAMMA)
COLOR mask(vec2 pos)
{
#if CRT_MASK == 3
	return vec3(1.0);
#else
	float p = OutputSize.y >= 1600.0 ? 6.0 : OutputSize.y >= 900.0 ? 3.0 : 2.0;
	float x = floor(pos.x), y = floor(pos.y);
	COLOR m;
	/* no mod() and no branch per pixel (p is the same for the whole draw): fract() of the pixel's centre
	   over a period, (n + 0.5) / p, lands clear of the steps that a float's rounding could move */
#if CRT_MASK == 2
	/* delta: each row of dots a part of a triad over from the one above */
	x += step(0.5, fract((floor(y / max(p / 3.0, 1.0)) + 0.5) * 0.5)) * floor(p / 2.0);
#endif
	if (p == 2.0) {
		/* two pixels: green, then red and blue */
		float lit = 2.0 - MASK_DARK;
		m = mix(vec3(MASK_DARK, lit, MASK_DARK), vec3(lit, MASK_DARK, lit), step(0.5, fract((x + 0.5) * 0.5)));
	} else {
		float lit = 3.0 - 2.0 * MASK_DARK;
		float i = floor(fract((x + 0.5) / p) * 3.0);	/* 0 1 2: red, green, blue */
		vec3 on = vec3(1.0 - step(0.5, i), step(0.5, i) - step(1.5, i), step(1.5, i));
		m = MASK_DARK + (lit - MASK_DARK) * on;
	}
#if CRT_MASK == 1
	/* slot: each triad broken by a dark row every slot, the next triad's half a slot lower */
	{
		float rows = p == 6.0 ? 2.0 : 1.0, slot = 4.0 * rows;
		float odd = step(0.5, fract((floor((x + 0.5) / p) + 0.5) * 0.5));
		float r = fract((y + odd * slot * 0.5 + 0.5) / slot) * slot;	/* the row in the slot, + 0.5 */
		m = mix(vec3(MASK_DARK), m, step(rows, r));
		m *= slot / (slot - rows + rows * MASK_DARK);
	}
#endif
	return m;
#endif
}
#else
/* the full build keeps the mask code the owner approved on the Pi, as it was */
vec3 mask(vec2 pos)
{
#if CRT_MASK == 3
	return vec3(1.0);
#else
	float p = OutputSize.y >= 1600.0 ? 6.0 : OutputSize.y >= 900.0 ? 3.0 : 2.0;
	float x = floor(pos.x), y = floor(pos.y);
	vec3 m;
#if CRT_MASK == 2
	/* delta: each row of dots a part of a triad over from the one above */
	x += mod(floor(y / max(p / 3.0, 1.0)), 2.0) * floor(p / 2.0);
#endif
	if (p == 2.0) {
		/* two pixels: green, then red and blue */
		float lit = 2.0 - MASK_DARK;
		m = mod(x, 2.0) < 1.0 ? vec3(MASK_DARK, lit, MASK_DARK) : vec3(lit, MASK_DARK, lit);
	} else {
		float lit = 3.0 - 2.0 * MASK_DARK;
		float i = floor(mod(x, p) * 3.0 / p);
		m = vec3(MASK_DARK);
		if (i < 0.5)
			m.r = lit;
		else if (i < 1.5)
			m.g = lit;
		else
			m.b = lit;
	}
#if CRT_MASK == 1
	/* slot: each triad broken by a dark row every slot, the next triad's half a slot lower */
	{
		float rows = p == 6.0 ? 2.0 : 1.0, slot = 4.0 * rows;
		float t = floor(x / p);
		if (mod(y + mod(t, 2.0) * slot * 0.5, slot) < rows)
			m = vec3(MASK_DARK);
		m *= slot / (slot - rows + rows * MASK_DARK);
	}
#endif
	return m;
#endif
}
#endif

void main()
{
	vec2 p = uv * TextureSize;
	float line = floor(p.y - 0.5);	/* the line above the pixel's centre; the one below is line + 1 */
	float f = p.y - 0.5 - line;	/* how far below that line's centre, in lines */
#ifdef CRT_LITE
	float u = uv.x;	/* across: the plain bilinear read */
#else
	float x = p.x - 0.5;
	float fx = clamp((fract(x) - 0.5) * SHARPNESS + 0.5, 0.0, 1.0);
	float u = (floor(x) + fx + 0.5) / TextureSize.x;
#endif
	COLOR e0 = texture2D(Texture, vec2(u, (line + 0.5) / TextureSize.y)).rgb;
	COLOR e1 = texture2D(Texture, vec2(u, (line + 1.5) / TextureSize.y)).rgb;
	/* no line above the first or below the last: the glass is black there */
	COLOR c0 = TO_LINEAR(e0) * step(0.0, line);
	COLOR c1 = TO_LINEAR(e1) * step(line + 1.5, TextureSize.y);
	/* the width from the colour as the game drew it: a mid grey is a mid-wide beam */
	COLOR col = c0 * beam(f, e0) + c1 * beam(1.0 - f, e1);
	col *= mask(gl_FragCoord.xy);
	gl_FragColor = vec4(TO_SCREEN(clamp(col, 0.0, 1.0)), 1.0);
}
#endif
