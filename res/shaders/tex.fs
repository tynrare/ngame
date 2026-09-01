// agent: grok-4.6 | 2026-08-31 | sample albedo magenta discard | 4eb915
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform float ng_time;
uniform vec2 ng_resolution;
uniform vec3 ng_tint;

out vec4 finalColor;

void main() {
  vec4 tex = texture(texture0, fragTexCoord);
  if (tex.r > 0.9 && tex.g < 0.08 && tex.b > 0.9) {
    discard;
  }
  vec3 n = normalize(fragNormal);
  float ndl = clamp(dot(n, normalize(vec3(0.35, 1.0, 0.25))), 0.0, 1.0);
  vec3 col = tex.rgb * ng_tint * (0.45 + 0.55 * ndl);
  finalColor = vec4(col, tex.a);
}
// agent: grok-4.6 | 2026-08-31 | sample albedo magenta discard | 4eb915
