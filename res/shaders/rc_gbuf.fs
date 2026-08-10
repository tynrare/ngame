// agent: composer-2.5 | 2026-08-10 | gbuf uvw A=1 opaque | 259762
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform vec3 ng_tint;
uniform vec3 ng_glow;
uniform vec3 ng_cam_pos;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform int ng_gbuf_mode; /* 0 albedo, 1 normal, 2 glow, 3 depth+world */

out vec4 finalColor;

void main() {
  if (ng_gbuf_mode == 1) {
    vec3 n = normalize(fragNormal) * 0.5 + 0.5;
    finalColor = vec4(n, 1.0);
  } else if (ng_gbuf_mode == 2) {
    finalColor = vec4(ng_glow, 1.0);
  } else if (ng_gbuf_mode == 3) {
    /* RGB = world UVW. A = 1 occupied (must be opaque — blend would kill RGB).
     * Distance = length(world - cam) in consumers. */
    vec3 uvw = (fragPosition - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
    finalColor = vec4(clamp(uvw, 0.0, 1.0), 1.0);
  } else {
    finalColor = vec4(ng_tint, 1.0);
  }
}
// agent: composer-2.5 | 2026-08-10 | gbuf uvw A=1 opaque | 259762
