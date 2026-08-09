// agent: composer-2.5 | 2026-08-09 | rc gbuf linear cam depth | e7a901
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform vec3 ng_tint;
uniform vec3 ng_glow;
uniform vec3 ng_cam_pos;
uniform int ng_gbuf_mode; /* 0 albedo, 1 normal, 2 glow, 3 depth */

out vec4 finalColor;

void main() {
  if (ng_gbuf_mode == 1) {
    vec3 n = normalize(fragNormal) * 0.5 + 0.5;
    finalColor = vec4(n, 1.0);
  } else if (ng_gbuf_mode == 2) {
    finalColor = vec4(ng_glow, 1.0);
  } else if (ng_gbuf_mode == 3) {
    /* Linear distance from camera (world units). Empty = 0. */
    float d = length(fragPosition - ng_cam_pos);
    finalColor = vec4(d, d, d, 1.0);
  } else {
    finalColor = vec4(ng_tint, 1.0);
  }
}
// agent: composer-2.5 | 2026-08-09 | rc gbuf linear cam depth | e7a901
