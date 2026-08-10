// agent: composer-2.5 | 2026-08-10 | decode depth * FAR | c6839c
/* Screen-resolve WS probe volume for irradiance debug. */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
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

const float FAR = 80.0;

vec3 world_from_uv_depth(vec2 uv, float d) {
  vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  vec3 dir = normalize(ng_cam_forward + ng_cam_right * (ndc.x * ng_tan_half_fov * ng_aspect) +
                       ng_cam_up * (ndc.y * ng_tan_half_fov));
  return ng_cam_pos + dir * d;
}

vec3 sample_probe_volume(vec3 world) {
  vec3 uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  if (uvw.x < 0.0 || uvw.y < 0.0 || uvw.z < 0.0 || uvw.x > 1.0 || uvw.y > 1.0 || uvw.z > 1.0) {
    return ng_sky * 0.1;
  }
  float res = max(ng_probe_res, 1.0);
  float x = uvw.x * (res - 1.0);
  float y = uvw.y * (res - 1.0);
  float z = uvw.z * (res - 1.0);
  float u = (x + 0.5) / res;
  float v = (y + z * res + 0.5) / (res * res);
  return texture(tex_irradiance_ws, vec2(u, v)).rgb;
}

void main() {
  vec2 uv = fragTexCoord;
  float enc = texture(tex_depth, uv).r;
  if (enc < 0.0005) {
    finalColor = vec4(0.0);
    return;
  }
  float d = enc * FAR;
  vec3 world = world_from_uv_depth(uv, d);
  world -= normalize(world - ng_cam_pos) * 0.08;
  finalColor = vec4(sample_probe_volume(world), 1.0);
}
// agent: composer-2.5 | 2026-08-10 | decode depth * FAR | c6839c
