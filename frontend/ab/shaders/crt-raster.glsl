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
  averages 1 - the picture keeps its brightness, only white clips.

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

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform vec2 TextureSize;
uniform vec2 OutputSize;
varying vec2 uv;
varying float boxVar;

#if defined(VERTEX)
uniform mat4 MVPMatrix;
attribute vec4 VertexCoord;
attribute vec4 TexCoord;

void main()
{
	float h = TextureSize.y / OutputSize.y;	/* a screen pixel's height in lines */
	boxVar = h * h / 12.0;
	uv = TexCoord.xy;
	gl_Position = MVPMatrix * VertexCoord;
}
#elif defined(FRAGMENT)

uniform sampler2D Texture;

/* a line's light at dy lines from its centre: a Gaussian of area 1, wider for a brighter colour c (as drawn) */
float beam(float dy, vec3 c)
{
	float s = mix(SIGMA_DARK, SIGMA_BRIGHT, max(c.r, max(c.g, c.b)));
	float v = s * s + boxVar;
	return exp(-dy * dy / (2.0 * v)) * inversesqrt(6.2831853 * v);
}

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

void main()
{
	vec2 p = uv * TextureSize;
	float line = floor(p.y - 0.5);	/* the line above the pixel's centre; the one below is line + 1 */
	float f = p.y - 0.5 - line;	/* how far below that line's centre, in lines */
	float x = p.x - 0.5;
	float fx = clamp((fract(x) - 0.5) * SHARPNESS + 0.5, 0.0, 1.0);
	float u = (floor(x) + fx + 0.5) / TextureSize.x;
	vec3 e0 = texture2D(Texture, vec2(u, (line + 0.5) / TextureSize.y)).rgb;
	vec3 e1 = texture2D(Texture, vec2(u, (line + 1.5) / TextureSize.y)).rgb;
	/* no line above the first or below the last: the glass is black there */
	vec3 c0 = pow(e0, vec3(INPUT_GAMMA)) * step(0.0, line);
	vec3 c1 = pow(e1, vec3(INPUT_GAMMA)) * step(line + 1.5, TextureSize.y);
	/* the width from the colour as the game drew it: a mid grey is a mid-wide beam */
	vec3 col = c0 * beam(f, e0) + c1 * beam(1.0 - f, e1);
	col *= mask(gl_FragCoord.xy);
	gl_FragColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / OUTPUT_GAMMA)), 1.0);
}
#endif
