// scale2x (AdvMAME2x) - the same rule as pcsx-abnxt's ab_scaler.c
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
  vec3 E = texture2D(Texture, c).rgb;
  vec3 B = texture2D(Texture, c - vec2(0.0, t.y)).rgb;
  vec3 H = texture2D(Texture, c + vec2(0.0, t.y)).rgb;
  vec3 D = texture2D(Texture, c - vec2(t.x, 0.0)).rgb;
  vec3 F = texture2D(Texture, c + vec2(t.x, 0.0)).rgb;
  vec3 o = E;
  if (B != H && D != F) {
    vec3 v = fp.y < 0.5 ? B : H;
    vec3 s = fp.x < 0.5 ? D : F;
    if (v == s) o = v;
  }
  gl_FragColor = vec4(o, 1.0);
}
#endif
