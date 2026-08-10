// agent: composer-2.5 | 2026-08-10 | WS T-merge same probe dir | 9fde66
/* Same (probe, dir) texel: near + (1-a_near) * far. */
in vec2 fragTexCoord;

uniform sampler2D tex_near;
uniform sampler2D tex_far;

out vec4 finalColor;

void main() {
  ivec2 sz = textureSize(tex_near, 0);
  vec2 uv = gl_FragCoord.xy / max(vec2(sz), vec2(1.0));
  vec4 near_r = texture(tex_near, uv);
  float a = clamp(near_r.a, 0.0, 1.0);
  if (a >= 0.999) {
    finalColor = near_r;
    return;
  }
  vec4 far_r = texture(tex_far, uv);
  float T = 1.0 - a;
  vec3 rad = near_r.rgb + T * far_r.rgb;
  float ao = a + T * clamp(far_r.a, 0.0, 1.0);
  finalColor = vec4(rad, ao);
}
// agent: composer-2.5 | 2026-08-10 | WS T-merge same probe dir | 9fde66
