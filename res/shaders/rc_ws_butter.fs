// agent: composer-2.5 | 2026-08-10 | butter FragCoord UV | f9046c
/* Butter: mix previous volume with fresh fill (same GL FragCoord space). */
in vec2 fragTexCoord;

uniform sampler2D tex_prev;
uniform sampler2D tex_curr;
uniform float ng_butter; /* 0 = keep prev, 1 = full curr */

out vec4 finalColor;

void main() {
  ivec2 sz = textureSize(tex_curr, 0);
  vec2 uv = gl_FragCoord.xy / max(vec2(sz), vec2(1.0));
  vec4 p = texture(tex_prev, uv);
  vec4 c = texture(tex_curr, uv);
  float k = clamp(ng_butter, 0.0, 1.0);
  finalColor = mix(p, c, k);
}
// agent: composer-2.5 | 2026-08-10 | butter FragCoord UV | f9046c
