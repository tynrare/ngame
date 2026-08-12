// agent: composer-2.5 | 2026-08-11 | GPU frustum cull BVH nodes | 812fdb
// agent: composer-2.5 | 2026-08-11 | fix frustum outward plane test | fd7a94
// agent: composer-2.5 | 2026-08-11 | AABB cull positive vertex | 6a53e5
// agent: composer-2.5 | 2026-08-12 | cull AABB epsilon pad | 514e69
// agent: composer-2.5 | 2026-08-12 | prim AABB cull match CPU | 886089
/* Classic 6-plane AABB frustum cull. One fragment = one index.
 * Pass 0 → node vis (BVH). Pass 1 → prim vis from tex_prim AABB (matches CPU).
 * Planes must be inward-facing. */
in vec2 fragTexCoord;

uniform sampler2D tex_bvh;
uniform sampler2D tex_node_vis;
uniform sampler2D tex_prim;
uniform vec4 ng_frustum[6];
uniform float ng_bvh_count;
uniform float ng_prim_count;
uniform int ng_cull_pass; /* 0=nodes, 1=prims */

out vec4 finalColor;

const int BVH_MAX = 128;
const int PRIM_MAX = 64;
const float AABB_PAD = 0.05;

vec4 bvh_col(int node, int col) {
  return texelFetch(tex_bvh, ivec2(col, node), 0);
}

/** Fully outside plane iff positive vertex (max along +n) is outside. */
bool aabb_outside_plane(vec3 bmin, vec3 bmax, vec4 plane) {
  vec3 n = plane.xyz;
  vec3 pv = vec3(n.x > 0.0 ? bmax.x : bmin.x, n.y > 0.0 ? bmax.y : bmin.y,
                 n.z > 0.0 ? bmax.z : bmin.z);
  return dot(n, pv) + plane.w < 0.0;
}

bool aabb_in_frustum(vec3 bmin, vec3 bmax) {
  for (int i = 0; i < 6; i++) {
    if (aabb_outside_plane(bmin, bmax, ng_frustum[i])) {
      return false;
    }
  }
  return true;
}

void main() {
  int x = int(floor(gl_FragCoord.x));
  if (ng_cull_pass == 0) {
    int ncount = int(clamp(ng_bvh_count, 0.0, float(BVH_MAX)));
    if (x < 0 || x >= ncount) {
      finalColor = vec4(0.0);
      return;
    }
    vec4 c0 = bvh_col(x, 0);
    vec4 c1 = bvh_col(x, 1);
    vec4 c2 = bvh_col(x, 2);
    vec3 bmin = c0.xyz - vec3(AABB_PAD);
    vec3 bmax = c1.xyz + vec3(AABB_PAD);
    float vis = aabb_in_frustum(bmin, bmax) ? 1.0 : 0.0;
    finalColor = vec4(vis, c2.x, float(x + 1), 1.0);
    return;
  }

  /* Pass 1: same AABB as CPU mod_render_rc_ws_prim_in_frustum. */
  int pcount = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  if (x < 0 || x >= pcount) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 ctr = texelFetch(tex_prim, ivec2(0, x), 0);
  vec4 halfv = texelFetch(tex_prim, ivec2(1, x), 0);
  vec3 bmin;
  vec3 bmax;
  if (ctr.w > 0.5) {
    float r = halfv.x;
    bmin = ctr.xyz - vec3(r);
    bmax = ctr.xyz + vec3(r);
  } else {
    bmin = ctr.xyz - halfv.xyz;
    bmax = ctr.xyz + halfv.xyz;
  }
  bmin -= vec3(AABB_PAD);
  bmax += vec3(AABB_PAD);
  float vis = aabb_in_frustum(bmin, bmax) ? 1.0 : 0.0;
  finalColor = vec4(vis, float(x + 1), 0.0, 1.0);
}
// agent: composer-2.5 | 2026-08-11 | GPU frustum cull BVH nodes | 812fdb
// agent: composer-2.5 | 2026-08-11 | fix frustum outward plane test | fd7a94
// agent: composer-2.5 | 2026-08-11 | AABB cull positive vertex | 6a53e5
// agent: composer-2.5 | 2026-08-12 | cull AABB epsilon pad | 514e69
// agent: composer-2.5 | 2026-08-12 | prim AABB cull match CPU | 886089
