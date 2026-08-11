// agent: composer-2.5 | 2026-08-11 | GPU frustum cull BVH nodes | 812fdb
// agent: composer-2.5 | 2026-08-11 | fix frustum outward plane test | fd7a94
// agent: composer-2.5 | 2026-08-11 | AABB cull positive vertex | 6a53e5
/* Classic 6-plane AABB frustum cull. One fragment = one index.
 * Pass 0 → node vis. Pass 1 → prim vis (copies leaf node flags).
 * Planes must be inward-facing. */
in vec2 fragTexCoord;

uniform sampler2D tex_bvh;
uniform sampler2D tex_node_vis;
uniform vec4 ng_frustum[6];
uniform float ng_bvh_count;
uniform float ng_prim_count;
uniform int ng_cull_pass; /* 0=nodes, 1=prims */

out vec4 finalColor;

const int BVH_MAX = 128;
const int PRIM_MAX = 64;

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
    float vis = aabb_in_frustum(c0.xyz, c1.xyz) ? 1.0 : 0.0;
    finalColor = vec4(vis, c2.x, float(x + 1), 1.0);
    return;
  }

  int pcount = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  if (x < 0 || x >= pcount) {
    finalColor = vec4(0.0);
    return;
  }
  int ncount = int(clamp(ng_bvh_count, 0.0, float(BVH_MAX)));
  float vis = 0.0;
  for (int i = 0; i < BVH_MAX; i++) {
    if (i >= ncount) {
      break;
    }
    int prim = int(bvh_col(i, 2).x);
    if (prim != x) {
      continue;
    }
    /* Sample both rows — raylib FBO Y flip. */
    vis = max(vis, max(texelFetch(tex_node_vis, ivec2(i, 0), 0).r,
                       texelFetch(tex_node_vis, ivec2(i, 1), 0).r));
  }
  finalColor = vec4(vis, float(x + 1), 0.0, 1.0);
}
// agent: composer-2.5 | 2026-08-11 | GPU frustum cull BVH nodes | 812fdb
// agent: composer-2.5 | 2026-08-11 | fix frustum outward plane test | fd7a94
// agent: composer-2.5 | 2026-08-11 | AABB cull positive vertex | 6a53e5
