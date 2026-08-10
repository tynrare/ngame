// agent: composer-2.5 | 2026-08-10 | integer 3D probe sample | 5fa5ec
/* Screen-resolve WS probe volume for irradiance debug. */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
uniform vec3 ng_sky;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

vec3 sample_probe_volume(vec3 world) {
  vec3 uvw = (world - ng_ws_origin) / max(ng_ws_size, vec3(0.001));
  if (uvw.x < 0.0 || uvw.y < 0.0 || uvw.z < 0.0 || uvw.x > 1.0 || uvw.y > 1.0 || uvw.z > 1.0) {
    return ng_sky * 0.1;
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
    finalColor = vec4(0.0);
    return;
  }
  vec3 world = ng_ws_origin + depth_pack.rgb * ng_ws_size;
  // agent: composer-2.5 | 2026-08-10 | view match compose sample | ae2fea
  world.y += 0.12;
  finalColor = vec4(sample_probe_volume(world), 1.0);
}
// agent: composer-2.5 | 2026-08-10 | view match compose sample | ae2fea
// agent: composer-2.5 | 2026-08-10 | integer 3D probe sample | 5fa5ec
