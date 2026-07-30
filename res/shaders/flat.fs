// agent: composer-2.5 | 2026-07-29 | flat solid tint shader | 9256cd
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform float ng_time;
uniform vec2 ng_resolution;
uniform vec3 ng_tint;

out vec4 finalColor;

void main() {
  vec3 n = normalize(fragNormal);
  float ndl = clamp(dot(n, normalize(vec3(0.35, 1.0, 0.25))), 0.0, 1.0);
  vec3 col = ng_tint * (0.35 + 0.65 * ndl);
  finalColor = vec4(col, 1.0);
}
// agent: composer-2.5 | 2026-07-29 | flat solid tint shader | 9256cd
