// agent: composer-2.5 | 2026-08-09 | WS compose sample probes | 635e06
in vec2 fragTexCoord;

uniform sampler2D tex_albedo;
uniform sampler2D tex_normal;
uniform sampler2D tex_glow;
uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance;
uniform float ng_gi_strength;
uniform vec3 ng_sky;
uniform vec3 ng_cam_pos;
uniform vec3 ng_cam_forward;
uniform vec3 ng_cam_right;
uniform vec3 ng_cam_up;
uniform float ng_tan_half_fov;
uniform float ng_aspect;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;

out vec4 finalColor;

vec3 world_from_uv_depth(vec2 uv, float d) {
  vec2 ndc = uv * 2.0 - 1.0;
  vec3 dir = normalize(ng_cam_forward + ng_cam_right * (ndc.x * ng_tan_half_fov * ng_aspect) +
                       ng_cam_up * (ndc.y * ng_tan_half_fov));
  return ng_cam_pos + dir * d;
}

vec3 sample_probe_volume(vec3 world) {
  vec3 uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  if (uvw.x < 0.0 || uvw.y < 0.0 || uvw.z < 0.0 || uvw.x > 1.0 || uvw.y > 1.0 || uvw.z > 1.0) {
    return ng_sky * 0.15;
  }
  float res = max(ng_probe_res, 1.0);
  float x = uvw.x * (res - 1.0);
  float y = uvw.y * (res - 1.0);
  float z = uvw.z * (res - 1.0);
  /* Atlas: width=res, height=res*res; slice z stacked in Y. */
  float u = (x + 0.5) / res;
  float v = (y + z * res + 0.5) / (res * res);
  return texture(tex_irradiance, vec2(u, v)).rgb;
}

void main() {
  vec2 uv = fragTexCoord;
  float d = texture(tex_depth, uv).r;
  if (d < 0.02) {
    finalColor = vec4(ng_sky * 0.25, 1.0);
    return;
  }

  vec3 albedo = texture(tex_albedo, uv).rgb;
  vec3 n = normalize(texture(tex_normal, uv).rgb * 2.0 - 1.0);
  vec3 glow = texture(tex_glow, uv).rgb;
  vec3 world = world_from_uv_depth(uv, d);
  vec3 irradiance = sample_probe_volume(world);

  vec3 l0 = normalize(vec3(0.45, 0.85, 0.2));
  vec3 l1 = normalize(vec3(-0.55, 0.35, 0.65));
  vec3 c0 = vec3(1.0, 0.95, 0.85);
  vec3 c1 = vec3(0.45, 0.55, 1.0);
  float ndl0 = max(dot(n, l0), 0.0);
  float ndl1 = max(dot(n, l1), 0.0);
  vec3 direct = albedo * (c0 * ndl0 + c1 * ndl1 * 0.55);
  vec3 ambient = albedo * 0.05;
  vec3 gi = albedo * irradiance * ng_gi_strength;
  finalColor = vec4(ambient + direct + gi + glow, 1.0);
}
// agent: composer-2.5 | 2026-08-09 | WS compose sample probes | 635e06
