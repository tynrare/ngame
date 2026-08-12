// agent: composer-2.5 | 2026-08-12 | even split-merge slot_cap free | b633c9
// agent: composer-2.5 | 2026-08-12 | split-merge no budget truncate | 816796
// agent: composer-2.5 | 2026-08-12 | probe apply incremental hash | 90579d
// agent: composer-2.5 | 2026-08-12 | stochastic split K lottery | b93c9c
/* Probe residency apply.
 * ng_apply_pass:
 *  1 = meta from slots (xyz center, a=10+lod)
 *  2 = open-address hash from slots (GPU rebuild; no CPU pack)
 *  3 = split-merge: budget gates split; stochastic lottery up to ng_split_k
 */
in vec2 fragTexCoord;

uniform sampler2D tex_work;
uniform sampler2D tex_keep;
uniform sampler2D tex_slots;
uniform float ng_work_count;
uniform float ng_slot_cap;
uniform float ng_budget;
uniform float ng_hash_size;
uniform float ng_world_cell;
uniform float ng_split_k;
uniform float ng_frame;
uniform int ng_apply_pass;

out vec4 finalColor;

const int WORK_MAX = 2048;
const int SLOT_MAX = 512;
const int HASH_MAX = 1024;
const int LOD_STRIDE = 1000;

uint cell_hash(int lod, ivec3 c) {
  uint h = uint(lod) * 2654435761u;
  h ^= uint(c.x) * 73856093u;
  h ^= uint(c.y) * 19349663u;
  h ^= uint(c.z) * 83492791u;
  return h;
}

uint stoch(int si, int frame) {
  uint h = uint(si) * 2654435761u;
  h ^= uint(frame) * 2246822519u;
  h ^= h >> 16;
  return h;
}

bool keep_at(int w) {
  float a = texelFetch(tex_keep, ivec2(w, 0), 0).r;
  float b = texelFetch(tex_keep, ivec2(w, 1), 0).r;
  return max(a, b) > 0.5;
}

int child_keep_count(int parent) {
  int nk = 0;
  for (int c = 0; c < 8; c++) {
    int w = parent * 8 + c;
    if (w >= WORK_MAX) {
      break;
    }
    if (keep_at(w)) {
      nk++;
    }
  }
  return nk;
}

bool emit_kept_child(int parent, int child_rank) {
  int r = 0;
  for (int c = 0; c < 8; c++) {
    int w = parent * 8 + c;
    if (w >= WORK_MAX) {
      break;
    }
    if (!keep_at(w)) {
      continue;
    }
    if (r == child_rank) {
      vec4 wv = texelFetch(tex_work, ivec2(w, 0), 0);
      if (length(wv.xyz) < 1e-6 && abs(wv.w) < 1e-6) {
        return false;
      }
      int pack_w = int(round(wv.w));
      int lod = pack_w % LOD_STRIDE;
      finalColor = vec4(round(wv.xyz), float(lod + 1));
      return true;
    }
    r++;
  }
  return false;
}

int count_used_slots(int scap) {
  int used = 0;
  for (int si = 0; si < SLOT_MAX; si++) {
    if (si >= scap) {
      break;
    }
    if (texelFetch(tex_slots, ivec2(si, 0), 0).a >= 0.5) {
      used++;
    }
  }
  return used;
}

void main() {
  if (ng_apply_pass == 1) {
    int si = int(floor(gl_FragCoord.y));
    int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
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
    float cell = max(ng_world_cell, 0.001) * exp2(float(lod));
    vec3 c = (s.xyz + vec3(0.5)) * cell;
    finalColor = vec4(c, 10.0 + float(lod));
    return;
  }

  if (ng_apply_pass == 3) {
    int r = int(floor(gl_FragCoord.x));
    int budget = int(clamp(ng_budget, 0.0, float(SLOT_MAX)));
    int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
    int split_k = int(clamp(ng_split_k, 0.0, 64.0));
    int frame = int(ng_frame);
    if (r < 0 || r >= scap) {
      finalColor = vec4(0.0);
      return;
    }
    int used = count_used_slots(scap);
    int free_n = scap - used;
    bool wave_ok = used < budget;
    int emit_count = 0;
    int splits_done = 0;
    for (int si = 0; si < SLOT_MAX; si++) {
      if (si >= scap) {
        break;
      }
      if (emit_count >= scap) {
        break;
      }
      vec4 s = texelFetch(tex_slots, ivec2(si, 0), 0);
      if (s.a < 0.5) {
        continue;
      }
      int lod = int(round(s.a)) - 1;
      bool do_split = false;
      int emit_n = 1;
      if (lod <= 0) {
        emit_n = 1;
      } else if (!wave_ok) {
        emit_n = 1;
      } else {
        int nk = child_keep_count(si);
        if (nk == 0) {
          emit_n = 0;
          free_n += 1;
          used -= 1;
        } else if (free_n + 1 >= nk && emit_count + nk <= scap) {
          bool lottery = int(stoch(si, frame) % 64u) < split_k;
          if (lottery && splits_done < split_k) {
            do_split = true;
            emit_n = nk;
            free_n -= (nk - 1);
            used += (nk - 1);
            splits_done++;
          } else {
            emit_n = 1;
          }
        } else {
          emit_n = 1;
        }
      }
      if (emit_n <= 0) {
        continue;
      }
      if (!do_split && emit_count + emit_n > scap) {
        break;
      }
      if (do_split && emit_count + emit_n > scap) {
        do_split = false;
        emit_n = 1;
        if (emit_count + emit_n > scap) {
          break;
        }
      }
      if (r >= emit_count && r < emit_count + emit_n) {
        if (do_split) {
          if (!emit_kept_child(si, r - emit_count)) {
            finalColor = vec4(0.0);
          }
        } else {
          finalColor = s;
        }
        return;
      }
      emit_count += emit_n;
    }
    finalColor = vec4(0.0);
    return;
  }

  /* pass 2: open-address hash */
  int h = int(floor(gl_FragCoord.x));
  int hsz = int(clamp(ng_hash_size, 1.0, float(HASH_MAX)));
  if (h < 0 || h >= hsz) {
    finalColor = vec4(0.0);
    return;
  }
  int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
  uint mask = uint(hsz - 1);
  uint occ[32];
  for (int i = 0; i < 32; i++) {
    occ[i] = 0u;
  }
  finalColor = vec4(0.0);
  for (int si = 0; si < SLOT_MAX; si++) {
    if (si >= scap) {
      break;
    }
    vec4 s = texelFetch(tex_slots, ivec2(si, 0), 0);
    if (s.a < 0.5) {
      continue;
    }
    int lod = int(round(s.a)) - 1;
    ivec3 c = ivec3(round(s.xyz));
    uint pos = cell_hash(lod, c) & mask;
    for (int step = 0; step < HASH_MAX; step++) {
      if (step >= hsz) {
        break;
      }
      uint wi = pos >> 5;
      uint bi = 1u << (pos & 31u);
      if ((occ[wi] & bi) == 0u) {
        occ[wi] |= bi;
        if (int(pos) == h) {
          finalColor =
              vec4(float(si + 1 + lod * LOD_STRIDE), float(c.x), float(c.y), float(c.z));
        }
        break;
      }
      pos = (pos + 1u) & mask;
    }
  }
}
// agent: composer-2.5 | 2026-08-12 | even split-merge slot_cap free | b633c9
// agent: composer-2.5 | 2026-08-12 | split-merge no budget truncate | 816796
// agent: composer-2.5 | 2026-08-12 | probe apply incremental hash | 90579d
// agent: composer-2.5 | 2026-08-12 | stochastic split K lottery | b93c9c
