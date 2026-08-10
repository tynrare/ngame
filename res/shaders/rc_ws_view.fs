// agent: composer-2.5 | 2026-08-10 | WS trilinear 8-tap sample | 84c51d
/* Screen-resolve WS probe volume for irradiance debug (matches compose). */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
uniform vec3 ng_sky;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

vec3 fetch_probe(float res, float ix, float iy, float iz) {
  float u = (ix + 0.5) / res;
  float v = (iy + iz * res + 0.5) / (res * res);
  return texture(tex_irradiance_ws, vec2(u, v)).rgb;
}

/** Manual 3D trilinear — not 2D-atlas bilinear (that shears Y into Z). */
vec3 sample_probe_volume(vec3 world) {
  vec3 uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  if (uvw.x < 0.0 || uvw.y < 0.0 || uvw.z < 0.0 || uvw.x > 1.0 || uvw.y > 1.0 || uvw.z > 1.0) {
    return ng_sky * 0.1;
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
    finalColor = vec4(0.0);
    return;
  }
  vec3 world = ng_ws_origin + depth_pack.rgb * ng_ws_size;
  world.y += 0.12;
  finalColor = vec4(sample_probe_volume(world), 1.0);
}
// agent: composer-2.5 | 2026-08-10 | WS trilinear 8-tap sample | 84c51d
