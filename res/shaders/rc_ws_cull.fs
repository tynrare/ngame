// agent: composer-2.5 | 2026-08-11 | GPU frustum cull BVH nodes | 812fdb
// agent: composer-2.5 | 2026-08-11 | fix frustum outward plane test | fd7a94
// agent: composer-2.5 | 2026-08-11 | AABB cull positive vertex | 6a53e5
// agent: composer-2.5 | 2026-08-12 | cull AABB epsilon pad | 514e69
// agent: composer-2.5 | 2026-08-12 | prim AABB cull match CPU | 886089
// agent: composer-2.5 | 2026-08-13 | BVH hierarchical cull passes | e5a1b2
/* BVH frustum cull. Pass 0: node frustum hit → G. Pass 1: depth propagate → R.
 * Pass 2: prim vis from leaf node R. Planes inward-facing. */
in vec2 fragTexCoord;

uniform sampler2D tex_bvh;
uniform sampler2D tex_node_vis;
uniform sampler2D tex_prim;
uniform vec4 ng_frustum[6];
uniform float ng_bvh_count;
uniform float ng_prim_count;
uniform float ng_bvh_depth;
uniform int ng_cull_pass; /* 0=node hit, 1=depth vis, 2=prim */

out vec4 finalColor;

const int BVH_MAX = 4096;
const int PRIM_MAX = 2048;
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

float node_frustum_hit(int node) {
  vec4 c0 = bvh_col(node, 0);
  vec4 c1 = bvh_col(node, 1);
  vec3 bmin = c0.xyz - vec3(AABB_PAD);
  vec3 bmax = c1.xyz + vec3(AABB_PAD);
  return aabb_in_frustum(bmin, bmax) ? 1.0 : 0.0;
}

void main() {
  int x = int(floor(gl_FragCoord.x));
  if (ng_cull_pass == 0) {
    int ncount = int(clamp(ng_bvh_count, 0.0, float(BVH_MAX)));
    if (x < 0 || x >= ncount) {
      finalColor = vec4(0.0);
      return;
    }
    float hit = node_frustum_hit(x);
    finalColor = vec4(0.0, hit, float(x + 1), 1.0);
    return;
  }

  if (ng_cull_pass == 1) {
    int ncount = int(clamp(ng_bvh_count, 0.0, float(BVH_MAX)));
    if (x < 0 || x >= ncount) {
      finalColor = vec4(0.0);
      return;
    }
    vec4 prev = texelFetch(tex_node_vis, ivec2(x, 0), 0);
    vec4 c2 = bvh_col(x, 2);
    vec4 c3 = bvh_col(x, 3);
    int node_depth = int(c3.x + 0.5);
    int want = int(ng_bvh_depth + 0.5);
    float fr = prev.g;
    if (node_depth != want) {
      finalColor = vec4(prev.r, fr, prev.b, 1.0);
      return;
    }
    int parent = int(c2.y + 0.5);
    float pvis = 1.0;
    if (parent >= 0) {
      pvis = texelFetch(tex_node_vis, ivec2(parent, 0), 0).r;
    }
    finalColor = vec4(fr * pvis, fr, float(x + 1), 1.0);
    return;
  }

  /* Pass 2: prim vis from BVH leaf node. */
  int pcount = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  if (x < 0 || x >= pcount) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 leaf = texelFetch(tex_prim, ivec2(6, x), 0);
  int leaf_node = int(leaf.x + 0.5) - 1;
  float vis = 0.0;
  if (leaf_node >= 0 && leaf_node < BVH_MAX) {
    vis = texelFetch(tex_node_vis, ivec2(leaf_node, 0), 0).r;
  }
  finalColor = vec4(vis, float(x + 1), 0.0, 1.0);
}
// agent: composer-2.5 | 2026-08-13 | BVH hierarchical cull passes | e5a1b2
