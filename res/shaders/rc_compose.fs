// agent: composer-2.5 | 2026-08-10 | brighter unity ambient floor GI | 99a1da
/* Direct + ambient + GI + glow. AMBIENT/DIRECTIONAL = 1; brighter bases + floor GI. */
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
uniform vec3 ng_cam_pos;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

/* Unity defaults — keep at 1; brightness via bases + fill bounce. */
const float AMBIENT = 1.0;
const float DIRECTIONAL = 1.0;
const float AMBIENT_ALBEDO = 0.22; /* ambient = albedo * this * AMBIENT */

void main() {
  vec2 uv = gl_FragCoord.xy / max(ng_resolution, vec2(1.0));
  vec4 depth_pack = texture(tex_depth, uv);
  if (depth_pack.a < 0.5) {
    finalColor = vec4(ng_sky * 0.18, 1.0);
    return;
  }

  vec4 alb_r = texture(tex_albedo, uv);
  vec4 n_m = texture(tex_normal, uv);
  vec3 albedo = alb_r.rgb;
  float rough = clamp(alb_r.a, 0.04, 1.0);
  float metal = clamp(n_m.a, 0.0, 1.0);
  vec3 n = normalize(n_m.rgb * 2.0 - 1.0);
  vec3 glow = texture(tex_glow, uv).rgb;

  float ws_w = max(ng_ws_weight, 0.0);
  float ss_w = max(ng_ss_weight, 0.0);
  vec3 irr_ws = texture(tex_irradiance_ws, uv).rgb;
  vec3 irr_ss = (ss_w > 0.0) ? texture(tex_irradiance_ss, uv).rgb : vec3(0.0);
  vec3 irr = irr_ws * ws_w + irr_ss * ss_w;

  vec3 world = ng_ws_origin + depth_pack.rgb * ng_ws_size;
  vec3 view_dir = normalize(ng_cam_pos - world);

  vec3 l0 = normalize(vec3(0.45, 0.85, 0.2));
  vec3 l1 = normalize(vec3(-0.55, 0.35, 0.65));
  vec3 c0 = vec3(1.15, 1.08, 0.95);
  vec3 c1 = vec3(0.55, 0.65, 1.15);
  float ndl0 = max(dot(n, l0), 0.0);
  float ndl1 = max(dot(n, l1), 0.0);

  vec3 diffuse = albedo * (c0 * ndl0 + c1 * ndl1 * 0.55);
  diffuse = mix(diffuse, diffuse * (1.0 - metal * 0.85), metal);

  float shininess = mix(8.0, 96.0, 1.0 - rough);
  vec3 h0 = normalize(l0 + view_dir);
  vec3 h1 = normalize(l1 + view_dir);
  float spec0 = pow(max(dot(n, h0), 0.0), shininess);
  float spec1 = pow(max(dot(n, h1), 0.0), shininess);
  vec3 spec_col = mix(vec3(0.04), albedo, metal);
  vec3 specular = spec_col * (c0 * spec0 + c1 * spec1 * 0.4) * (1.0 - rough * 0.65);

  vec3 ambient = albedo * AMBIENT_ALBEDO * AMBIENT;
  vec3 direct = (diffuse + specular) * DIRECTIONAL;

  /* GI scale default 1 via ng_gi_strength; metal cuts diffuse receive. */
  vec3 kd = mix(albedo, albedo * 0.2, metal);
  vec3 gi = kd * irr * ng_gi_strength;

  finalColor = vec4(ambient + direct + gi + glow, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | brighter unity ambient floor GI | 99a1da
