// agent: grok-4.6 | 2026-08-12 | probe collapse relax GPU | 4e5c2d
/* Collapse finest sibling groups → parent when unmet and used>=budget. Persist otherwise. */
in vec2 fragTexCoord;

uniform sampler2D tex_slots;
uniform sampler2D tex_unmet;
uniform float ng_slot_cap;
uniform float ng_budget;
uniform float ng_relax_k;

out vec4 finalColor;

const int SLOT_MAX = 512;

void main() {
  int si = int(floor(gl_FragCoord.x));
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  int budget = int(clamp(ng_budget, 0.0, float(SLOT_MAX)));
  int rk = int(clamp(ng_relax_k, 0.0, 64.0));
  if (si < 0 || si >= scap) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 s = texelFetch(tex_slots, ivec2(si, 0), 0);
  float unmet = texelFetch(tex_unmet, ivec2(0, 0), 0).r;
  int used = 0;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    if (texelFetch(tex_slots, ivec2(i, 0), 0).a >= 0.5) {
      used++;
    }
  }
  if (unmet < 0.5 || used < budget || rk <= 0 || s.a < 0.5) {
    finalColor = s;
    return;
  }

  int min_lod = 99;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    vec4 o = texelFetch(tex_slots, ivec2(i, 0), 0);
    if (o.a < 0.5) {
      continue;
    }
    int ol = int(round(o.a)) - 1;
    if (ol < min_lod) {
      min_lod = ol;
    }
  }
  if (min_lod >= 7 || min_lod < 0) {
    finalColor = s;
    return;
  }

  int lod = int(round(s.a)) - 1;
  if (lod != min_lod) {
    finalColor = s;
    return;
  }

  ivec3 ic = ivec3(round(s.xyz));
  int plod = lod + 1;
  ivec3 pc = ivec3(ic.x >> 1, ic.y >> 1, ic.z >> 1);

  int nsib = 0;
  int min_si = si;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    vec4 o = texelFetch(tex_slots, ivec2(i, 0), 0);
    if (o.a < 0.5) {
      continue;
    }
    int ol = int(round(o.a)) - 1;
    if (ol != lod) {
      continue;
    }
    ivec3 oc = ivec3(round(o.xyz));
    if ((oc.x >> 1) != pc.x || (oc.y >> 1) != pc.y || (oc.z >> 1) != pc.z) {
      continue;
    }
    nsib++;
    if (i < min_si) {
      min_si = i;
    }
  }
  if (nsib < 2) {
    finalColor = s;
    return;
  }

  /* Rank collapse groups by min_si; only first rk groups collapse. */
  int better_groups = 0;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    vec4 o = texelFetch(tex_slots, ivec2(i, 0), 0);
    if (o.a < 0.5) {
      continue;
    }
    int ol = int(round(o.a)) - 1;
    if (ol != lod) {
      continue;
    }
    ivec3 oc = ivec3(round(o.xyz));
    ivec3 opc = ivec3(oc.x >> 1, oc.y >> 1, oc.z >> 1);
    int gmin = i;
    for (int j = 0; j < SLOT_MAX; j++) {
      if (j >= scap) {
        break;
      }
      vec4 q = texelFetch(tex_slots, ivec2(j, 0), 0);
      if (q.a < 0.5) {
        continue;
      }
      if (int(round(q.a)) - 1 != lod) {
        continue;
      }
      ivec3 qc = ivec3(round(q.xyz));
      if ((qc.x >> 1) == opc.x && (qc.y >> 1) == opc.y && (qc.z >> 1) == opc.z && j < gmin) {
        gmin = j;
      }
    }
    if (gmin == i && gmin < min_si) {
      better_groups++;
    }
  }
  if (better_groups >= rk) {
    finalColor = s;
    return;
  }

  if (si == min_si) {
    /* Emit parent key in this slot. */
    finalColor = vec4(float(pc.x), float(pc.y), float(pc.z), float(plod + 1));
    return;
  }
  finalColor = vec4(0.0);
}
// agent: grok-4.6 | 2026-08-12 | probe collapse relax GPU | 4e5c2d
