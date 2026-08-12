// agent: composer-2.5 | 2026-08-12 | GPU split-merge open-hash | c84c54
/* Probe residency apply.
 * ng_apply_pass:
 *  0 = Gen0 slots from keep ranks (xyz=ic, a=lod+1)
 *  1 = meta from slots (xyz center, a=10+lod)
 *  2 = open-address hash from slots
 *  3 = split-merge: unsplit parents + kept children (read tex_slots, write other RT)
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

bool keep_at(int w) {
  float a = texelFetch(tex_keep, ivec2(w, 0), 0).r;
  float b = texelFetch(tex_keep, ivec2(w, 1), 0).r;
  return max(a, b) > 0.5;
}

int keep_rank_work(int rank) {
  int nwork = int(clamp(ng_work_count, 0.0, float(WORK_MAX)));
  int r = 0;
  for (int w = 0; w < WORK_MAX; w++) {
    if (w >= nwork) {
      break;
    }
    if (!keep_at(w)) {
      continue;
    }
    if (r == rank) {
      return w;
    }
    r++;
  }
  return -1;
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

/** Emit the rank-th kept child of parent into finalColor; false if missing. */
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
  if (ng_apply_pass == 0) {
    int si = int(floor(gl_FragCoord.x));
    int budget = int(clamp(ng_budget, 0.0, float(SLOT_MAX)));
    int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
    if (si < 0 || si >= scap || si >= budget) {
      finalColor = vec4(0.0);
      return;
    }
    int wi = keep_rank_work(si);
    if (wi < 0) {
      finalColor = vec4(0.0);
      return;
    }
    vec4 w = texelFetch(tex_work, ivec2(wi, 0), 0);
    if (length(w.xyz) < 1e-6 && abs(w.w) < 1e-6) {
      finalColor = vec4(0.0);
      return;
    }
    int pack_w = int(round(w.w));
    int lod = pack_w % LOD_STRIDE;
    finalColor = vec4(round(w.xyz), float(lod + 1));
    return;
  }

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
    /* Split-merge: copy CPU apply_split_surface wave (snapshot all parents). */
    int r = int(floor(gl_FragCoord.x));
    int budget = int(clamp(ng_budget, 0.0, float(SLOT_MAX)));
    int scap = int(clamp(ng_slot_cap, 1.0, float(SLOT_MAX)));
    if (r < 0 || r >= scap || r >= budget) {
      finalColor = vec4(0.0);
      return;
    }
    int used0 = count_used_slots(scap);
    bool can_split = used0 < budget;
    int emit_count = 0;
    for (int si = 0; si < SLOT_MAX; si++) {
      if (si >= scap) {
        break;
      }
      vec4 s = texelFetch(tex_slots, ivec2(si, 0), 0);
      if (s.a < 0.5) {
        continue;
      }
      int lod = int(round(s.a)) - 1;
      int nk = 0;
      bool do_split = false;
      int emit_n = 1;
      if (lod <= 0) {
        emit_n = 1;
      } else {
        nk = child_keep_count(si);
        if (nk == 0) {
          emit_n = 0;
        } else if (!can_split || emit_count + nk > budget) {
          emit_n = 1;
        } else {
          do_split = true;
          emit_n = nk;
        }
      }
      if (emit_n <= 0) {
        continue;
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

  /* pass 2: open-address hash (CPU rebuild_hash order). */
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
// agent: composer-2.5 | 2026-08-12 | GPU split-merge open-hash | c84c54
