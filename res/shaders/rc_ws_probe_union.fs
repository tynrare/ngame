// agent: composer-2.5 | 2026-08-12 | probe union AABB shader | d899eb
/* Vis-union AABB of culled-visible prims → 2×1 RT: x=0 umin, x=1 umax (rgb). */
in vec2 fragTexCoord;

uniform sampler2D tex_prim;
uniform sampler2D tex_prim_vis;
uniform float ng_prim_count;

out vec4 finalColor;

const int PRIM_MAX = 2048;

vec4 prim_col(int row, int col) {
  return texelFetch(tex_prim, ivec2(col, row), 0);
}

float prim_vis_at(int prim) {
  float a = texelFetch(tex_prim_vis, ivec2(prim, 0), 0).r;
  float b = texelFetch(tex_prim_vis, ivec2(prim, 1), 0).r;
  return max(a, b);
}

void prim_aabb(int row, out vec3 bmin, out vec3 bmax) {
  vec4 ctr = prim_col(row, 0);
  vec4 halfv = prim_col(row, 1);
  if (ctr.w > 0.5) {
    float r = halfv.x;
    bmin = ctr.xyz - vec3(r);
    bmax = ctr.xyz + vec3(r);
  } else {
    float br = max(halfv.w, max(halfv.x, max(halfv.y, halfv.z)));
    bmin = ctr.xyz - vec3(br);
    bmax = ctr.xyz + vec3(br);
  }
}

void main() {
  int x = int(floor(gl_FragCoord.x));
  int nprim = int(clamp(ng_prim_count, 0.0, float(PRIM_MAX)));
  bool any = false;
  vec3 umin = vec3(1e30);
  vec3 umax = vec3(-1e30);
  for (int p = 0; p < PRIM_MAX; p++) {
    if (p >= nprim) {
      break;
    }
    if (prim_vis_at(p) < 0.5) {
      continue;
    }
    vec3 bmin;
    vec3 bmax;
    prim_aabb(p, bmin, bmax);
    umin = min(umin, bmin);
    umax = max(umax, bmax);
    any = true;
  }
  if (!any) {
    finalColor = vec4(0.0);
    return;
  }
  if (x <= 0) {
    finalColor = vec4(umin, 1.0);
  } else {
    finalColor = vec4(umax, 1.0);
  }
}
// agent: composer-2.5 | 2026-08-12 | probe union AABB shader | d899eb
