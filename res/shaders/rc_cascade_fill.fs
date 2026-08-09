// agent: composer-2.5 | 2026-08-09 | SS RC cascade fill | 9d2e40
// agent: composer-2.5 | 2026-08-09 | depth hit hit_frac fill | 39e073
// agent: composer-2.5 | 2026-08-09 | fill miss zero early hit | ce86cb
in vec2 fragTexCoord;

uniform sampler2D tex_depth;
uniform sampler2D tex_albedo;
uniform sampler2D tex_glow;
uniform vec2 ng_resolution;
uniform int ng_cascade; /* 0,1,2 */
uniform vec3 ng_sky;

out vec4 finalColor;

const float FAR = 80.0;
const float EPS = 0.02;
const float THICKNESS = 0.12;
const float EMIT_BOOST = 1.25;
const float BOUNCE_SCALE = 0.4;

int dir_count_for(int c) {
  if (c <= 0) return 4;
  if (c == 1) return 12;
  return 24;
}

float interval0(int c) {
  if (c <= 0) return 0.0;
  if (c == 1) return 16.0;
  return 64.0;
}

float interval1(int c) {
  if (c <= 0) return 16.0;
  if (c == 1) return 64.0;
  return 256.0;
}

vec2 dir_from_index(int i, int n) {
  float a = 6.2831853 * (float(i) + 0.5) / float(n);
  return vec2(cos(a), sin(a));
}

void main() {
  vec2 uv = fragTexCoord;
  float my_d = texture(tex_depth, uv).r;
  if (my_d < EPS) {
    finalColor = vec4(0.0, 0.0, 0.0, 0.0);
    return;
  }

  int n = dir_count_for(ng_cascade);
  float t0 = interval0(ng_cascade);
  float t1 = interval1(ng_cascade);
  /* Fewer steps → early exit restores FPS vs full-interval thickness skips. */
  float step_px = max(2.0, (t1 - t0) / 8.0);
  vec2 px = 1.0 / ng_resolution;

  vec3 rad = vec3(0.0);
  float hit_sum = 0.0;
  float wsum = 0.0;

  for (int i = 0; i < 32; i++) {
    if (i >= n) break;
    vec2 dir = dir_from_index(i, n);
    bool hit = false;
    vec3 hit_rad = vec3(0.0);
    for (float t = t0 + step_px; t <= t1; t += step_px) {
      vec2 p = uv + dir * t * px;
      if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0) {
        break;
      }
      float zd = texture(tex_depth, p).r;
      if (zd <= EPS || zd >= FAR) {
        continue;
      }
      /* Depth edge or closer surface — skip same-depth neighbors. */
      float dd = abs(zd - my_d);
      if (dd > THICKNESS || zd < my_d - THICKNESS * 0.5) {
        vec3 g = texture(tex_glow, p).rgb;
        vec3 a = texture(tex_albedo, p).rgb;
        hit_rad = g * EMIT_BOOST + a * BOUNCE_SCALE;
        hit = true;
        break;
      }
    }
    /* Miss rays contribute 0 rgb; merge fills from parent via (1-hit). */
    rad += hit_rad;
    if (hit) {
      hit_sum += 1.0;
    }
    wsum += 1.0;
  }

  float inv = 1.0 / max(wsum, 1.0);
  finalColor = vec4(rad * inv, hit_sum * inv);
}
// agent: composer-2.5 | 2026-08-09 | SS RC cascade fill | 9d2e40
// agent: composer-2.5 | 2026-08-09 | depth hit hit_frac fill | 39e073
// agent: composer-2.5 | 2026-08-09 | fill miss zero early hit | ce86cb
