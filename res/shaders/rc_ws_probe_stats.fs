// agent: composer-2.5 | 2026-08-12 | probe stats occupancy shader | 7a4e04
/* 1×1 occupancy: r=used g=freeable(=scap-used) b=budget a=1. */
in vec2 fragTexCoord;

uniform sampler2D tex_slots;
uniform float ng_slot_cap;
uniform float ng_budget;

out vec4 finalColor;

const int SLOT_MAX = 512;

void main() {
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  int budget = int(clamp(ng_budget, 0.0, float(SLOT_MAX)));
  int used = 0;
  for (int i = 0; i < SLOT_MAX; i++) {
    if (i >= scap) {
      break;
    }
    if (texelFetch(tex_slots, ivec2(i, 0), 0).a >= 0.5) {
      used++;
    }
  }
  float freeable = float(scap - used);
  finalColor = vec4(float(used), freeable, float(budget), 1.0);
}
// agent: composer-2.5 | 2026-08-12 | probe stats occupancy shader | 7a4e04
