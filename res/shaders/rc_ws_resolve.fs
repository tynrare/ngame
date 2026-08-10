// agent: composer-2.5 | 2026-08-10 | resolve softer SH blend | 6c23c4
/* Soft-nearest L1 SH × N → irradiance. Smoother face blend; less banding. */
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

const float SOFT_BAND = 0.32;

vec3 fetch_band(float res, float ix, float iy, float iz, float band, vec2 sres) {
  float u = (band * res + ix + 0.5) / sres.x;
  float v = (iy + iz * res + 0.5) / sres.y;
  return texture(tex_sh, vec2(u, v)).rgb;
}

vec3 eval_sh(float res, float ix, float iy, float iz, vec3 n, vec2 sres) {
  vec3 l0 = fetch_band(res, ix, iy, iz, 0.0, sres);
  vec3 lx = fetch_band(res, ix, iy, iz, 1.0, sres);
  vec3 ly = fetch_band(res, ix, iy, iz, 2.0, sres);
  vec3 lz = fetch_band(res, ix, iy, iz, 3.0, sres);
  return max(l0 + lx * n.x + ly * n.y + lz * n.z, vec3(0.0));
}

/** Smooth weight in [0,1] near cell face (0 at center-ish, 1 at edge). */
float face_w(float fr, float band) {
  float lo = 1.0 - smoothstep(0.0, band, fr);
  float hi = smoothstep(1.0 - band, 1.0, fr);
  return max(lo, hi);
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
  world += n * 0.08;

  vec3 uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  if (uvw.x < 0.0 || uvw.y < 0.0 || uvw.z < 0.0 || uvw.x > 1.0 || uvw.y > 1.0 || uvw.z > 1.0) {
    finalColor = vec4(ng_sky * 0.1, 1.0);
    return;
  }

  float res = max(ng_probe_res, 1.0);
  float n1 = res - 1.0;
  vec3 pf = clamp(uvw, 0.0, 0.999999) * res;
  vec3 i0 = floor(pf);
  vec3 fr = fract(pf);
  vec2 sres = max(ng_sh_res, vec2(1.0));

  float ix = clamp(i0.x, 0.0, n1);
  float iy = clamp(i0.y, 0.0, n1);
  float iz = clamp(i0.z, 0.0, n1);

  vec3 acc = eval_sh(res, ix, iy, iz, n, sres);
  float wsum = 1.0;

  float wx = face_w(fr.x, SOFT_BAND) * 0.45;
  if (wx > 0.001) {
    float nx = (fr.x < 0.5) ? (ix - 1.0) : (ix + 1.0);
    if (nx >= 0.0 && nx <= n1) {
      acc += eval_sh(res, nx, iy, iz, n, sres) * wx;
      wsum += wx;
    }
  }
  float wy = face_w(fr.y, SOFT_BAND) * 0.45;
  if (wy > 0.001) {
    float ny = (fr.y < 0.5) ? (iy - 1.0) : (iy + 1.0);
    if (ny >= 0.0 && ny <= n1) {
      acc += eval_sh(res, ix, ny, iz, n, sres) * wy;
      wsum += wy;
    }
  }
  float wz = face_w(fr.z, SOFT_BAND) * 0.45;
  if (wz > 0.001) {
    float nz = (fr.z < 0.5) ? (iz - 1.0) : (iz + 1.0);
    if (nz >= 0.0 && nz <= n1) {
      acc += eval_sh(res, ix, iy, nz, n, sres) * wz;
      wsum += wz;
    }
  }

  finalColor = vec4(acc / max(wsum, 1e-3), 1.0);
}
// agent: composer-2.5 | 2026-08-10 | resolve softer SH blend | 6c23c4
