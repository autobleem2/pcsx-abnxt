/*
    hqx - HQ2x / HQ3x (HQX_SCALE 2 or 3) on the GPU, as the smoothing pass: the same rule as the CPU's
    frontend/ab/hqx, worked out per output pixel with a lookup table instead of hqx's pattern switches.

    Copyright (C) 2003 Maxim Stepin, (C) 2010 Cameron Zemek, (C) 2014 Jules Blok (libretro glsl-shaders'
    hqx/single-pass, whose lookup tables ab_hqx_lut.h carries); this GLSL ES 1.00 rewrite (C) AutoBleem
    team, 2026. LGPL 2.1 or later - see frontend/ab/hqx/hq2x.c.

Each output pixel lies in one of the source pixel E's HQX_SCALE x HQX_SCALE parts. The 8 neighbours of E
that differ from it (in YUV, by hqx's thresholds) make the pattern (0..255), the 4 edges between them the
crossing (0..15); the table (AuxTexture: a column per pattern, a row per crossing and part) gives the
weights of E, the neighbour across the part's corner and the two beside it - 9 + 1 reads, no branches.
*/

#ifndef HQX_SCALE
#define HQX_SCALE 2
#endif

#if defined(VERTEX)
attribute vec4 VertexCoord;
attribute vec4 TexCoord;
varying vec2 uv;
uniform mat4 MVPMatrix;
void main() { gl_Position = MVPMatrix * VertexCoord; uv = TexCoord.xy; }
#elif defined(FRAGMENT)
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;	/* the place inside a pixel of a 640 wide frame: more than mediump's fractions */
#else
precision mediump float;
#endif
#endif
varying vec2 uv;
uniform sampler2D Texture;
uniform vec2 TextureSize;
uniform sampler2D AuxTexture;
uniform vec2 AuxSize;

/* the three rows of RGB -> YUV; c * M = (y, u, v) */
const mat3 YUV = mat3(0.299072266, 0.586914062, 0.114013672,
                      -0.168945312, -0.331054688, 0.5,
                      0.5, -0.418945312, -0.0809936523);
const vec3 THRESHOLD = vec3(0.188232422, 0.0274505615, 0.0235290527);	/* hqx's 48, 7, 6 of 255 */

float diff(vec3 a, vec3 b)
{
	return any(greaterThan(abs(a - b), THRESHOLD)) ? 1.0 : 0.0;
}

vec3 yuv_at(vec2 c)
{
	return texture2D(Texture, c).rgb * YUV;
}

void main()
{
	vec2 ps = 1.0 / TextureSize;
	vec2 p = uv * TextureSize;
	vec2 fp = fract(p);
	vec2 c = (floor(p) + 0.5) * ps;	/* E's centre */
	vec2 quad = sign(fp - 0.5);	/* towards the part's corner; 0 on the middle row/column of HQ3x */
	float S = float(HQX_SCALE);

	vec3 e = texture2D(Texture, c).rgb;
	vec3 corner = texture2D(Texture, c + ps * quad).rgb;
	vec3 side_x = texture2D(Texture, c + vec2(ps.x * quad.x, 0.0)).rgb;
	vec3 side_y = texture2D(Texture, c + vec2(0.0, ps.y * quad.y)).rgb;

	/* 1 2 3 / 4 E 6 / 7 8 9, row 1 above */
	vec3 w5 = e * YUV;
	vec3 w1 = yuv_at(c + vec2(-ps.x, -ps.y));
	vec3 w2 = yuv_at(c + vec2(0.0, -ps.y));
	vec3 w3 = yuv_at(c + vec2(ps.x, -ps.y));
	vec3 w4 = yuv_at(c + vec2(-ps.x, 0.0));
	vec3 w6 = yuv_at(c + vec2(ps.x, 0.0));
	vec3 w7 = yuv_at(c + vec2(-ps.x, ps.y));
	vec3 w8 = yuv_at(c + vec2(0.0, ps.y));
	vec3 w9 = yuv_at(c + vec2(ps.x, ps.y));

	float pattern = diff(w5, w1) + 2.0 * diff(w5, w2) + 4.0 * diff(w5, w3) + 8.0 * diff(w5, w4) +
		16.0 * diff(w5, w6) + 32.0 * diff(w5, w7) + 64.0 * diff(w5, w8) + 128.0 * diff(w5, w9);
	float crossing = diff(w4, w2) + 2.0 * diff(w2, w6) + 4.0 * diff(w8, w4) + 8.0 * diff(w6, w8);
	vec2 part = floor(fp * S);

	vec4 w = texture2D(AuxTexture, (vec2(pattern, crossing * S * S + part.x + part.y * S) + 0.5) / AuxSize);
	w /= dot(w, vec4(1.0));
	gl_FragColor = vec4(w.x * e + w.y * corner + w.z * side_x + w.w * side_y, 1.0);
}
#endif
