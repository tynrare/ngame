// agent: composer-2.5 | 2026-08-12 | probe gen children shader | 68bd89
// agent: composer-2.5 | 2026-08-13 | gen shader split gate | 45d714
/* Emit up to 8 children per used parent slot into work RT.
 * work[x]: parent = x/8, child = x%8; empty if parent unused or lod==0. */
in vec2 fragTexCoord;

uniform sampler2D tex_slots;
uniform sampler2D tex_unmet;
uniform float ng_slot_cap;
uniform float ng_work_count;

out vec4 finalColor;

const int SLOT_MAX = 512;
const int WORK_MAX = 2048;
const int LOD_STRIDE = 1000;

void main() {
  if (texelFetch(tex_unmet, ivec2(0, 0), 0).b < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  int x = int(floor(gl_FragCoord.x));
  int nwork = int(clamp(ng_work_count, 0.0, float(WORK_MAX)));
  if (x < 0 || x >= nwork) {
    finalColor = vec4(0.0);
    return;
  }
  int parent = x / 8;
  int child = x - parent * 8;
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  if (parent < 0 || parent >= scap) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 s = texelFetch(tex_slots, ivec2(parent, 0), 0);
  if (s.a < 0.5) {
    finalColor = vec4(0.0);
    return;
  }
  int lod = int(round(s.a)) - 1;
  if (lod <= 0) {
    finalColor = vec4(0.0);
    return;
  }
  int child_lod = lod - 1;
  ivec3 base = ivec3(round(s.xyz)) * 2;
  ivec3 c = base + ivec3(child & 1, (child >> 1) & 1, (child >> 2) & 1);
  /* pack lod only — keep uses ng_probe_any against all vis prims */
  finalColor = vec4(float(c.x), float(c.y), float(c.z), float(child_lod));
}
// agent: composer-2.5 | 2026-08-13 | gen shader split gate | 45d714
// agent: composer-2.5 | 2026-08-12 | probe gen children shader | 68bd89
