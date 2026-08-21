// agent: grok-4.6 | 2026-08-21 | compose drop Lambert sun | c1fbc8
/* kd * RC irradiance + glow; tiny albedo ambient while pages fill. */
in vec2 fragTexCoord;

uniform sampler2D tex_albedo;
uniform sampler2D tex_normal;
uniform sampler2D tex_glow;
uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
uniform float ng_gi_strength;
uniform vec3 ng_sky;
uniform vec3 ng_cam_pos;
uniform vec2 ng_resolution;

out vec4 finalColor;

const float AMBIENT_ALBEDO = 0.06;

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
  float metal = clamp(n_m.a, 0.0, 1.0);
  vec3 glow = texture(tex_glow, uv).rgb;
  vec3 irr = texture(tex_irradiance_ws, uv).rgb;
  vec3 kd = mix(albedo, albedo * 0.2, metal);
  vec3 gi = kd * irr * ng_gi_strength;
  vec3 ambient = albedo * AMBIENT_ALBEDO;
  finalColor = vec4(ambient + gi + glow, 1.0);
}
// agent: grok-4.6 | 2026-08-21 | compose drop Lambert sun | c1fbc8
