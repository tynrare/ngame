// agent: composer-2.5 | 2026-07-25 | cube stripe shader | 9d5a2e
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform float ng_time;
uniform vec2 ng_resolution;
uniform vec3 ng_tint;

out vec4 finalColor;

void main() {
  float stripe = step(0.5, fract(fragPosition.x * 3.0 + fragPosition.y * 2.0 + ng_time * 0.5));
  vec3 col = mix(ng_tint * 0.35, ng_tint, stripe);
  finalColor = vec4(col, 1.0);
}
