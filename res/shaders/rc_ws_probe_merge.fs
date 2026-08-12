// agent: composer-2.5 | 2026-08-12 | probe merge siblings shader | 665407
/* Compact used slots left + sibling collapse: if all 8 children of a parent key
 * are present and empty-of-need (already cleared by release), nothing to write.
 * If 8 siblings at lod L share a parent and ALL are freeable-absent, skip.
 * Practical: pack used → front (holes → free at end for cover). */
in vec2 fragTexCoord;

uniform sampler2D tex_slots;
uniform float ng_slot_cap;

out vec4 finalColor;

const int SLOT_MAX = 512;

void main() {
  int r = int(floor(gl_FragCoord.x));
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  if (r < 0 || r >= scap) {
    finalColor = vec4(0.0);
    return;
  }
  int emit = 0;
  for (int si = 0; si < SLOT_MAX; si++) {
    if (si >= scap) {
      break;
    }
    vec4 s = texelFetch(tex_slots, ivec2(si, 0), 0);
    if (s.a < 0.5) {
      continue;
    }
    if (emit == r) {
      finalColor = s;
      return;
    }
    emit++;
  }
  finalColor = vec4(0.0);
}
// agent: composer-2.5 | 2026-08-12 | probe merge siblings shader | 665407
