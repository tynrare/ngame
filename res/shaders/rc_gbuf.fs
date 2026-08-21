// agent: composer-2.5 | 2026-08-10 | gbuf pack rough metal alphas | 6b62e9
// agent: composer-2.5 | 2026-08-10 | gbuf A geometry not clip | e7e855
// agent: composer-2.5 | 2026-08-11 | gbuf cam-relative depth pack | 4615c1
// agent: composer-2.5 | 2026-08-12 | gbuf prim probe id modes | 9f50c1
// agent: composer-2.5 | 2026-08-12 | gbuf FS drop discard cull | 45e363
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
flat in float vInstId;

uniform vec3 ng_tint;
uniform vec3 ng_glow;
uniform float ng_roughness;
uniform float ng_metalness;
uniform vec3 ng_cam_pos;
// agent: grok-4.6 | 2026-08-21 | gbuf drop unused clip uniforms | e11a5a
uniform int ng_gbuf_mode; /* 0 albedo+rough, 1 normal+metal, 2 glow, 3 depth+world */

out vec4 finalColor;

void main() {
  if (ng_gbuf_mode == 1) {
    vec3 n = normalize(fragNormal) * 0.5 + 0.5;
    finalColor = vec4(n, clamp(ng_metalness, 0.0, 1.0));
  } else if (ng_gbuf_mode == 2) {
    finalColor = vec4(ng_glow, 1.0);
  } else if (ng_gbuf_mode == 3) {
    /* RGB = world XYZ (float RT). A = geometry. No clip UVW — avoids RGBA8 clamp. */
    finalColor = vec4(fragPosition, 1.0);
  } else if (ng_gbuf_mode == 4) {
    finalColor = vec4(0.0);
  } else if (ng_gbuf_mode == 5) {
    finalColor = vec4(0.0);
  } else {
    /* RGB = albedo, A = roughness */
    finalColor = vec4(ng_tint, clamp(ng_roughness, 0.04, 1.0));
  }
}
// agent: grok-4.6 | 2026-08-21 | gbuf drop unused clip uniforms | e11a5a
// agent: composer-2.5 | 2026-08-10 | gbuf pack rough metal alphas | 6b62e9
// agent: composer-2.5 | 2026-08-10 | gbuf A geometry not clip | e7e855
// agent: composer-2.5 | 2026-08-11 | gbuf cam-relative depth pack | 4615c1
// agent: composer-2.5 | 2026-08-12 | gbuf prim probe id modes | 9f50c1
// agent: composer-2.5 | 2026-08-12 | gbuf FS drop discard cull | 45e363
