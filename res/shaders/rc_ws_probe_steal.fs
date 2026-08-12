// agent: grok-4.6 | 2026-08-12 | stable steal no pressure | 75180b
/* Steal up to ng_steal_k finest (lod<=2) only when used > budget. Stable score. */
in vec2 fragTexCoord;

uniform sampler2D tex_slots;
uniform float ng_slot_cap;
uniform float ng_budget;
uniform float ng_steal_k;

out vec4 finalColor;

const int SLOT_MAX = 512;

uint stoch_stable(int si) {
  uint h = uint(si) * 2654435761u;
  h ^= h >> 16;
  return h;
}

uint steal_score(int si, int lod) {
  int fine = clamp(3 - lod, 0, 3);
  return uint(fine) * 100000u + (stoch_stable(si) % 100000u);
}

void main() {
  int si = int(floor(gl_FragCoord.x));
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  int budget = int(clamp(ng_budget, 0.0, float(SLOT_MAX)));
  int k = int(clamp(ng_steal_k, 0.0, 64.0));
  if (si < 0 || si >= scap) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 s = texelFetch(tex_slots, ivec2(si, 0), 0);
  if (s.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  int lod = int(round(s.a)) - 1;
  if (lod > 2) {
    finalColor = s;
    return;
  }

  int used = 0;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    if (texelFetch(tex_slots, ivec2(i, 0), 0).a >= 0.5) {
      used++;
    }
  }
  int quota = min(k, max(used - budget, 0));
  if (quota <= 0) {
    finalColor = s;
    return;
  }

  uint my = steal_score(si, lod);
  int better = 0;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    vec4 o = texelFetch(tex_slots, ivec2(i, 0), 0);
    if (o.a < 0.5) {
      continue;
    }
    int ol = int(round(o.a)) - 1;
    if (ol > 2) {
      continue;
    }
    uint sc = steal_score(i, ol);
    if (sc > my || (sc == my && i < si)) {
      better++;
    }
  }
  if (better < quota) {
    finalColor = vec4(0.0);
    return;
  }
  finalColor = s;
}
// agent: grok-4.6 | 2026-08-12 | stable steal no pressure | 75180b
