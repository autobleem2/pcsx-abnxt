// Eagle (2x) - each output quarter takes the corner colour when that corner and its two edge neighbours agree;
// the rule pcsx-abnxt's ab_scaler.c eagle2x uses
#if defined(VERTEX)
attribute vec4 VertexCoord;
attribute vec4 TexCoord;
varying vec2 uv;
uniform mat4 MVPMatrix;
void main() { gl_Position = MVPMatrix * VertexCoord; uv = TexCoord.xy; }
#elif defined(FRAGMENT)
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 uv;
uniform sampler2D Texture;
uniform vec2 TextureSize;
void main() {
  vec2 t = 1.0 / TextureSize;
  vec2 c = (floor(uv * TextureSize) + 0.5) * t;
  vec2 fp = fract(uv * TextureSize);
  vec2 dx = vec2(fp.x < 0.5 ? -t.x : t.x, 0.0);
  vec2 dy = vec2(0.0, fp.y < 0.5 ? -t.y : t.y);
  vec3 E = texture2D(Texture, c).rgb;
  vec3 S = texture2D(Texture, c + dx).rgb;       // side neighbour of this quarter
  vec3 V = texture2D(Texture, c + dy).rgb;       // vertical neighbour
  vec3 K = texture2D(Texture, c + dx + dy).rgb;  // corner
  gl_FragColor = vec4((S == V && V == K) ? K : E, 1.0);
}
#endif
