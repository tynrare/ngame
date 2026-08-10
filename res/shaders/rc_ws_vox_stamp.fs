// agent: composer-2.5 | 2026-08-10 | frustum-fit GPU vox stamp | d7c874
/* Stamp one world AABB into VOX slice atlas (Y-stacked Z). FragCoord → voxel. */
in vec2 fragTexCoord;

uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_vox_res;
uniform vec3 ng_box_center;
uniform vec3 ng_box_half;
uniform vec3 ng_lit; /* premultiplied emit*boost + alb*bounce */

out vec4 finalColor;

void main() {
  float res = max(ng_vox_res, 1.0);
  float vx = floor(gl_FragCoord.x);
  float vyz = floor(gl_FragCoord.y);
  float vy = mod(vyz, res);
  float vz = floor(vyz / res);
  if (vx >= res || vy >= res || vz >= res) {
    discard;
  }
  vec3 uvw = (vec3(vx, vy, vz) + 0.5) / res;
  vec3 world = ng_ws_origin + uvw * ng_ws_size;
  vec3 d = abs(world - ng_box_center);
  if (d.x > ng_box_half.x || d.y > ng_box_half.y || d.z > ng_box_half.z) {
    discard;
  }
  finalColor = vec4(ng_lit, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | frustum-fit GPU vox stamp | d7c874
