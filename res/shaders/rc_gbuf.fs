// agent: composer-2.5 | 2026-08-10 | depth encode d/FAR | 44f6bf
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform vec3 ng_tint;
uniform vec3 ng_glow;
uniform vec3 ng_cam_pos;
uniform int ng_gbuf_mode; /* 0 albedo, 1 normal, 2 glow, 3 depth */

out vec4 finalColor;

/* RGBA8 stores [0,1]; world distance = enc * FAR. Keep in sync with readers. */
const float FAR = 80.0;

void main() {
  if (ng_gbuf_mode == 1) {
    vec3 n = normalize(fragNormal) * 0.5 + 0.5;
    finalColor = vec4(n, 1.0);
  } else if (ng_gbuf_mode == 2) {
    finalColor = vec4(ng_glow, 1.0);
  } else if (ng_gbuf_mode == 3) {
    /* Encoded linear cam distance. Empty = 0. */
    float d = length(fragPosition - ng_cam_pos);
    float enc = clamp(d / FAR, 0.0, 1.0);
    finalColor = vec4(enc, enc, enc, 1.0);
  } else {
    finalColor = vec4(ng_tint, 1.0);
  }
}
// agent: composer-2.5 | 2026-08-10 | depth encode d/FAR | 44f6bf
