// agent: composer-2.5 | 2026-08-10 | integer 3D probe sample | fe128e
/* Direct + WS GI. UV from gl_FragCoord. Nearest probe in 3D, then atlas texel. */
in vec2 fragTexCoord;

uniform sampler2D tex_albedo;
uniform sampler2D tex_normal;
uniform sampler2D tex_glow;
uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
uniform float ng_gi_strength;
uniform vec3 ng_sky;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

vec3 sample_probe_volume(vec3 world) {
  vec3 uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  if (uvw.x < 0.0 || uvw.y < 0.0 || uvw.z < 0.0 || uvw.x > 1.0 || uvw.y > 1.0 || uvw.z > 1.0) {
    return ng_sky * 0.12;
  }
  float res = max(ng_probe_res, 1.0);
  /* 3D nearest — not 2D atlas nearest (that shears Y into Z). */
  vec3 p = floor(clamp(uvw, 0.0, 0.999999) * res);
  float u = (p.x + 0.5) / res;
  float v = (p.y + p.z * res + 0.5) / (res * res);
  return texture(tex_irradiance_ws, vec2(u, v)).rgb;
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
  /* Lift off floor/walls so volume sample is in empty air cells. */
  // agent: composer-2.5 | 2026-08-10 | compose bias up floor | 36e0c1
  world += n * 0.12;
  vec3 irr_ws = sample_probe_volume(world);

  vec3 l0 = normalize(vec3(0.45, 0.85, 0.2));
  vec3 l1 = normalize(vec3(-0.55, 0.35, 0.65));
  vec3 c0 = vec3(1.0, 0.95, 0.85);
  vec3 c1 = vec3(0.45, 0.55, 1.0);
  float ndl0 = max(dot(n, l0), 0.0);
  float ndl1 = max(dot(n, l1), 0.0);
  /* Keep direct modest so WS bleed reads in final. */
  // agent: composer-2.5 | 2026-08-10 | weaken direct for bleed | f0ad76
  vec3 direct = albedo * (c0 * ndl0 + c1 * ndl1 * 0.55) * 0.35;
  vec3 ambient = albedo * 0.04;
  vec3 gi = albedo * irr_ws * ng_gi_strength;
  finalColor = vec4(ambient + direct + gi + glow, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | compose bias up floor | 36e0c1
// agent: composer-2.5 | 2026-08-10 | integer 3D probe sample | fe128e
// agent: composer-2.5 | 2026-08-10 | weaken direct for bleed | f0ad76
