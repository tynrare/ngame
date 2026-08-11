// agent: composer-2.5 | 2026-08-11 | B64 wire GPU prio passes | f7b538
/* Reduce tex_prio → 1×1 argmax. ng_mode 0=promote 1=relax. ng_exclude = slot+1.
 * Output B=slot_id+1 A=flags R=score. Readback only this 1×1 (O(1) bandwidth). */
in vec2 fragTexCoord;

uniform sampler2D tex_prio;
uniform float ng_slot_cap;
uniform float ng_mode;
uniform float ng_exclude[16];

out vec4 finalColor;

bool excluded(float sid1) {
  for (int i = 0; i < 16; i++) {
    if (ng_exclude[i] > 0.5 && abs(ng_exclude[i] - sid1) < 0.5) {
      return true;
    }
  }
  return false;
}

void main() {
  float best = -1.0;
  vec4 bestv = vec4(0.0);
  int n = int(ng_slot_cap);
  for (int i = 0; i < 512; i++) {
    if (i >= n) {
      break;
    }
    vec4 p = texelFetch(tex_prio, ivec2(i, 0), 0);
    if (p.a < 0.5) {
      continue;
    }
    if (excluded(p.b)) {
      continue;
    }
    float s = (ng_mode < 0.5) ? p.r : p.g;
    if (ng_mode < 0.5 && p.a < 5.0) {
      continue; /* need splittable flag */
    }
    if (s > best) {
      best = s;
      bestv = p;
    }
  }
  finalColor = bestv;
}
// agent: composer-2.5 | 2026-08-11 | B64 wire GPU prio passes | f7b538
