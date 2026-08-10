// agent: composer-2.5 | 2026-08-10 | resolve trilinear SH texelFetch | 199537
/* Trilinear L1 SH × N → irradiance. texelFetch avoids atlas Y/Z shear. */
in vec2 fragTexCoord;

uniform sampler2D tex_sh;
uniform sampler2D tex_depth;
uniform sampler2D tex_normal;
uniform vec3 ng_sky;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform vec2 ng_sh_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

vec3 fetch_band(float res, float ix, float iy, float iz, float band) {
  int x = int(band * res + ix);
  int y = int(iy + iz * res);
  return texelFetch(tex_sh, ivec2(x, y), 0).rgb;
}

vec3 eval_sh(float res, float ix, float iy, float iz, vec3 n) {
  vec3 l0 = fetch_band(res, ix, iy, iz, 0.0);
  vec3 lx = fetch_band(res, ix, iy, iz, 1.0);
  vec3 ly = fetch_band(res, ix, iy, iz, 2.0);
  vec3 lz = fetch_band(res, ix, iy, iz, 3.0);
  return max(l0 + lx * n.x + ly * n.y + lz * n.z, vec3(0.0));
}

void main() {
  vec2 uv = gl_FragCoord.xy / max(ng_resolution, vec2(1.0));
  vec4 depth_pack = texture(tex_depth, uv);
  if (depth_pack.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }

  vec3 n = normalize(texture(tex_normal, uv).rgb * 2.0 - 1.0);
  vec3 world = ng_ws_origin + depth_pack.rgb * ng_ws_size;
  world += n * 0.06;

  vec3 uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  if (uvw.x < 0.0 || uvw.y < 0.0 || uvw.z < 0.0 || uvw.x > 1.0 || uvw.y > 1.0 || uvw.z > 1.0) {
    finalColor = vec4(ng_sky * 0.1, 1.0);
    return;
  }

  float res = max(ng_probe_res, 1.0);
  float n1 = res - 1.0;
  /* Probe centers at (i+0.5)/res — sample in that continuous grid. */
  vec3 pf = clamp(uvw, 0.0, 0.999999) * res - 0.5;
  vec3 i0 = floor(pf);
  vec3 f = clamp(pf - i0, 0.0, 1.0);
  i0 = clamp(i0, vec3(0.0), vec3(n1));
  vec3 i1 = min(i0 + 1.0, vec3(n1));

  vec3 c000 = eval_sh(res, i0.x, i0.y, i0.z, n);
  vec3 c100 = eval_sh(res, i1.x, i0.y, i0.z, n);
  vec3 c010 = eval_sh(res, i0.x, i1.y, i0.z, n);
  vec3 c110 = eval_sh(res, i1.x, i1.y, i0.z, n);
  vec3 c001 = eval_sh(res, i0.x, i0.y, i1.z, n);
  vec3 c101 = eval_sh(res, i1.x, i0.y, i1.z, n);
  vec3 c011 = eval_sh(res, i0.x, i1.y, i1.z, n);
  vec3 c111 = eval_sh(res, i1.x, i1.y, i1.z, n);

  vec3 c00 = mix(c000, c100, f.x);
  vec3 c10 = mix(c010, c110, f.x);
  vec3 c01 = mix(c001, c101, f.x);
  vec3 c11 = mix(c011, c111, f.x);
  vec3 c0 = mix(c00, c10, f.y);
  vec3 c1 = mix(c01, c11, f.y);
  vec3 outc = mix(c0, c1, f.z);

  finalColor = vec4(outc, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | resolve trilinear SH texelFetch | 199537
