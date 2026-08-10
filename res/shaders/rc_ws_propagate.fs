// agent: composer-2.5 | 2026-08-09 | WS atlas stamp butter FS | a8dda5
/* One deterministic propagate step on world XZ atlas (recursive bleed). */
in vec2 fragTexCoord;

uniform sampler2D tex_atlas;
uniform vec2 ng_texel; /* 1/width, 1/height */
uniform float ng_spread;

out vec4 finalColor;

void main() {
  vec2 uv = fragTexCoord;
  vec4 c = texture(tex_atlas, uv);
  vec3 acc = c.rgb;
  float wsum = 1.0;
  float s = max(ng_spread, 0.0);
  /* Cross + diagonals — fixed kernel, no noise. */
  vec2 o[8];
  o[0] = vec2(1.0, 0.0);
  o[1] = vec2(-1.0, 0.0);
  o[2] = vec2(0.0, 1.0);
  o[3] = vec2(0.0, -1.0);
  o[4] = vec2(1.0, 1.0);
  o[5] = vec2(1.0, -1.0);
  o[6] = vec2(-1.0, 1.0);
  o[7] = vec2(-1.0, -1.0);
  for (int i = 0; i < 8; i++) {
    vec3 n = texture(tex_atlas, uv + o[i] * ng_texel).rgb;
    float w = s * 0.12;
    acc += n * w;
    wsum += w;
  }
  finalColor = vec4(acc / max(wsum, 0.001), 1.0);
}
// agent: composer-2.5 | 2026-08-09 | WS atlas stamp butter FS | a8dda5
