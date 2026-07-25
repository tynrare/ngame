// agent: composer-2.5 | 2026-07-25 | sphere rim pulse shader | 8c4f1d
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
  vec3 viewDir = normalize(-fragPosition);
  float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 3.0);
  float pulse = 0.5 + 0.5 * sin(ng_time * 2.0);
  vec3 col = ng_tint * (0.25 + rim * pulse);
  finalColor = vec4(col, 1.0);
}
