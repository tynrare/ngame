// agent: composer-2.5 | 2026-08-10 | hard-nearest WS cell sample | cc2e66
/* Irradiance debug: blit resolved screen WS irr. */
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance_ws;
uniform vec3 ng_sky;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_probe_res;
uniform vec2 ng_resolution;

out vec4 finalColor;

void main() {
  vec2 uv = gl_FragCoord.xy / max(ng_resolution, vec2(1.0));
  vec4 depth_pack = texture(tex_depth, uv);
  if (depth_pack.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  finalColor = vec4(texture(tex_irradiance_ws, uv).rgb, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | hard-nearest WS cell sample | cc2e66
