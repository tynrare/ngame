// agent: composer-2.5 | 2026-08-10 | restore glow in compose | bef51c
/* Direct + gi*(ws·WS+ss·SS)*albedo + glow. SS does not march glow; bloom later. */
in vec2 fragTexCoord;

uniform sampler2D tex_albedo;
uniform sampler2D tex_normal;
uniform sampler2D tex_glow;
uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
uniform sampler2D tex_irradiance_ss;
uniform float ng_gi_strength;
uniform float ng_ws_weight;
uniform float ng_ss_weight;
uniform vec3 ng_sky;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

/** Atlas texel for integer probe (ix,iy,iz); POINT filter — no 2D bilinear. */
vec3 fetch_probe(float res, float ix, float iy, float iz) {
  float u = (ix + 0.5) / res;
  float v = (iy + iz * res + 0.5) / (res * res);
  return texture(tex_irradiance_ws, vec2(u, v)).rgb;
}

/** Manual 3D trilinear — not 2D-atlas bilinear (that shears Y into Z). */
vec3 sample_probe_volume(vec3 world) {
  vec3 uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  if (uvw.x < 0.0 || uvw.y < 0.0 || uvw.z < 0.0 || uvw.x > 1.0 || uvw.y > 1.0 || uvw.z > 1.0) {
    return ng_sky * 0.12;
  }
  float res = max(ng_probe_res, 1.0);
  float n1 = res - 1.0;
  vec3 f = uvw * res - 0.5;
  vec3 i0 = floor(f);
  vec3 w = fract(f);
  float x0 = clamp(i0.x, 0.0, n1);
  float y0 = clamp(i0.y, 0.0, n1);
  float z0 = clamp(i0.z, 0.0, n1);
  float x1 = clamp(i0.x + 1.0, 0.0, n1);
  float y1 = clamp(i0.y + 1.0, 0.0, n1);
  float z1 = clamp(i0.z + 1.0, 0.0, n1);
  vec3 c000 = fetch_probe(res, x0, y0, z0);
  vec3 c100 = fetch_probe(res, x1, y0, z0);
  vec3 c010 = fetch_probe(res, x0, y1, z0);
  vec3 c110 = fetch_probe(res, x1, y1, z0);
  vec3 c001 = fetch_probe(res, x0, y0, z1);
  vec3 c101 = fetch_probe(res, x1, y0, z1);
  vec3 c011 = fetch_probe(res, x0, y1, z1);
  vec3 c111 = fetch_probe(res, x1, y1, z1);
  vec3 c00 = mix(c000, c100, w.x);
  vec3 c10 = mix(c010, c110, w.x);
  vec3 c01 = mix(c001, c101, w.x);
  vec3 c11 = mix(c011, c111, w.x);
  vec3 c0 = mix(c00, c10, w.y);
  vec3 c1 = mix(c01, c11, w.y);
  return mix(c0, c1, w.z);
}

void main() {
  vec2 uv = gl_FragCoord.xy / max(ng_resolution, vec2(1.0));
  vec4 depth_pack = texture(tex_depth, uv);
  if (depth_pack.a < 0.5) {
    finalColor = vec4(ng_sky * 0.25, 1.0);
    return;
  }

  vec3 albedo = texture(tex_albedo, uv).rgb;
  vec3 n = normalize(texture(tex_normal, uv).rgb * 2.0 - 1.0);
  vec3 glow = texture(tex_glow, uv).rgb;

  vec3 world = ng_ws_origin + depth_pack.rgb * ng_ws_size;
  world += n * 0.12;
  vec3 irr_ws = sample_probe_volume(world);
  // agent: composer-2.5 | 2026-08-10 | compose skip ss sample when 0 | d0fadf
  float ws_w = max(ng_ws_weight, 0.0);
  float ss_w = max(ng_ss_weight, 0.0);
  vec3 irr_ss = (ss_w > 0.0) ? texture(tex_irradiance_ss, uv).rgb : vec3(0.0);

  vec3 l0 = normalize(vec3(0.45, 0.85, 0.2));
  vec3 l1 = normalize(vec3(-0.55, 0.35, 0.65));
  vec3 c0 = vec3(1.0, 0.95, 0.85);
  vec3 c1 = vec3(0.45, 0.55, 1.0);
  float ndl0 = max(dot(n, l0), 0.0);
  float ndl1 = max(dot(n, l1), 0.0);
  vec3 direct = albedo * (c0 * ndl0 + c1 * ndl1 * 0.55);
  vec3 ambient = albedo * 0.04;
  vec3 irr = irr_ws * ws_w + irr_ss * ss_w;
  vec3 gi = albedo * irr * ng_gi_strength;
  // agent: composer-2.5 | 2026-08-10 | restore glow in compose | bef51c
  finalColor = vec4(ambient + direct + gi + glow, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | WS trilinear 8-tap sample | cc47c1
// agent: composer-2.5 | 2026-08-10 | restore glow in compose | bef51c
// agent: composer-2.5 | 2026-08-10 | compose skip ss sample when 0 | d0fadf
