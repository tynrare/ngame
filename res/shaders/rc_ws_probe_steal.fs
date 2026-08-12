// agent: composer-2.5 | 2026-08-12 | stochastic steal densest K | 8fcdea
/* Steal up to ng_steal_k finest/dense slots when used > budget.
 * Density proxy = low lod + stochastic hash(si, frame). Draw slots → slots_b. */
in vec2 fragTexCoord;

uniform sampler2D tex_slots;
uniform float ng_slot_cap;
uniform float ng_budget;
uniform float ng_steal_k;
uniform float ng_frame;

out vec4 finalColor;

const int SLOT_MAX = 512;

uint stoch(int si, int frame) {
  uint h = uint(si) * 2654435761u;
  h ^= uint(frame) * 2246822519u;
  h ^= h >> 16;
  return h;
}

int count_used(int scap) {
  int n = 0;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    if (texelFetch(tex_slots, ivec2(i, 0), 0).a >= 0.5) {
      n++;
    }
  }
  return n;
}

/** Higher = more steal-worthy (fine lod + hash). */
uint steal_score(int si, int lod, int frame) {
  int fine = clamp(3 - lod, 0, 3);
  return uint(fine) * 100000u + (stoch(si, frame) % 100000u);
}

void main() {
  int si = int(floor(gl_FragCoord.x));
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  int budget = int(clamp(ng_budget, 0.0, float(SLOT_MAX)));
  int k = int(clamp(ng_steal_k, 0.0, 64.0));
  int frame = int(ng_frame);
  if (si < 0 || si >= scap) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 s = texelFetch(tex_slots, ivec2(si, 0), 0);
  if (s.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  int used = count_used(scap);
  int excess = used - budget;
  int quota = min(k, max(excess, 0));
  if (quota <= 0) {
    finalColor = s;
    return;
  }
  int lod = int(round(s.a)) - 1;
  if (lod > 2) {
    finalColor = s;
    return;
  }
  uint my = steal_score(si, lod, frame);
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
    uint sc = steal_score(i, ol, frame);
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
// agent: composer-2.5 | 2026-08-12 | stochastic steal densest K | 8fcdea
