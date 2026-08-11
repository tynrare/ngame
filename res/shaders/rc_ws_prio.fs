// agent: composer-2.5 | 2026-08-11 | B66 want-have balanced prio | 09c3bd
/* B.6.6 slot priority. One texel = one slot.
 * R=promote G=coarsen B=slot+1 A=flags (1 used,+2 coarsen_boost,+4 promote)
 * Promote = (have-want)/(d+ε) when too coarse; coarsen ∝ fine² + over-fine. */
in vec2 fragTexCoord;

uniform sampler2D tex_meta;
uniform vec3 ng_eye;
uniform vec3 ng_forward;
uniform vec3 ng_ws_origin;
uniform vec3 ng_ws_size;
uniform float ng_slot_cap;
uniform float ng_cell;
uniform float ng_tan_half_fov;
uniform float ng_aspect;
uniform float ng_lod_enter[8];

out vec4 finalColor;

float cell_size(int lod) {
  return ng_cell * float(1 << lod);
}

int lod_for_dist(float d) {
  for (int L = 0; L < 8; L++) {
    if (d <= ng_lod_enter[L]) {
      return L;
    }
  }
  return 7;
}

/** 1=in cone, 0=outside sides, -1=behind. */
int leaf_view(vec3 c, vec3 eye, vec3 forward, float thv, float asp) {
  vec3 f = normalize(forward);
  if (dot(f, f) < 1e-8) {
    f = vec3(0.0, 0.0, 1.0);
  }
  vec3 v = c - eye;
  float along = dot(v, f);
  if (along <= 0.05) {
    return -1;
  }
  vec3 r = abs(f.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
  vec3 cx = normalize(cross(f, r));
  vec3 ux = cross(cx, f);
  float x = dot(v, cx);
  float y = dot(v, ux);
  float lim_y = along * max(thv, 0.05);
  float lim_x = lim_y * max(asp, 0.1);
  if (abs(x) > lim_x * 1.05 || abs(y) > lim_y * 1.05) {
    return 0;
  }
  return 1;
}

void main() {
  int si = int(floor(fragTexCoord.x * max(ng_slot_cap, 1.0)));
  if (si < 0 || float(si) >= ng_slot_cap) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 m = texelFetch(tex_meta, ivec2(0, si), 0);
  if (m.a < 0.5) {
    finalColor = vec4(0.0, 0.0, float(si + 1), 0.0);
    return;
  }
  int lod = int(floor(mod(m.a, 10.0)));
  if (lod < 0) {
    lod = 0;
  }
  if (lod > 7) {
    lod = 7;
  }
  float cell = cell_size(lod);
  vec3 c = m.xyz;
  float d = length(c - ng_eye);
  int want = lod_for_dist(d);
  vec3 amin = ng_ws_origin;
  vec3 amax = ng_ws_origin + ng_ws_size;
  vec3 halfc = vec3(cell * 0.5);
  bool oov_clip = any(lessThan(c + halfc, amin)) || any(greaterThan(c - halfc, amax));
  float thv = ng_tan_half_fov > 0.05 ? ng_tan_half_fov : 0.414;
  float asp = ng_aspect > 0.1 ? ng_aspect : 1.0;
  int view = leaf_view(c, ng_eye, ng_forward, thv, asp);
  bool can_coarsen = oov_clip || lod < 7;
  int fine = 7 - lod;

  float promote = 0.0;
  float coarsen = 0.0;
  float flags = 1.0;
  if (can_coarsen) {
    coarsen = float(fine * fine) * 100.0 + d * 0.05 + 0.01;
    if (lod < want) {
      coarsen += float(want - lod) * 2000.0;
    }
    if (oov_clip) {
      coarsen += 3.0e6;
      flags += 2.0;
    } else if (view < 0) {
      coarsen += 2.0e5;
      flags += 2.0;
    } else if (view == 0) {
      coarsen += 5.0e4;
      flags += 2.0;
    }
  }
  if (!oov_clip && view == 1 && lod > want && lod > 0) {
    promote = float(lod - want) / (d + 0.5);
    flags += 4.0;
  }
  finalColor = vec4(promote, coarsen, float(si + 1), flags);
}
// agent: composer-2.5 | 2026-08-11 | B66 want-have balanced prio | 09c3bd
