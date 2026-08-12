/* agent: grok-4.6 | 2026-08-12 | rc_ws probe smoke tool | 585808 */
/* agent: grok-4.6 | 2026-08-12 | vis-change smoke scenarios | 0fd409 */
/**
 * Headless CPU probe oracle smoke.
 * Build: cmake --build build --target ng_rc_ws_probe_smoke
 * Run:   ./build/ng_rc_ws_probe_smoke
 */
#include "client/render_rc_ws.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void fill_box(NgRcWsPrim *p, float x, float y, float z, float hx, float hy, float hz) {
  memset(p, 0, sizeof(*p));
  p->center[0] = x;
  p->center[1] = y;
  p->center[2] = z;
  p->half[0] = hx;
  p->half[1] = hy;
  p->half[2] = hz;
  p->quat[3] = 1.0f;
  p->type = 0;
  p->albedo[0] = p->albedo[1] = p->albedo[2] = 0.7f;
  p->lit[0] = p->lit[1] = p->lit[2] = 1.0f;
}

static void fill_playground8(NgRcWsCtx *ws) {
  /* Live RC playground spread (~±4); one coarse cell per box at cover_lod. */
  const float pos[8][3] = {{-4, 1, -4}, {4, 1, -4}, {-4, 1, 4}, {4, 1, 4},
                           {-2, 1, 0},  {2, 1, 0},  {0, 1, -2}, {0, 1, 2}};
  for (int i = 0; i < 8; i++) {
    fill_box(&ws->prims[i], pos[i][0], pos[i][1], pos[i][2], 0.8f, 0.8f, 0.8f);
  }
  ws->prim_count = 8;
}

static void fill_scene8(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-12 | cull_out scene separation | 72e8d7
  /* A (0..3) and B (4..7) separated so even lod-7 cells (~51m) cannot span both. */
  const float pos[8][3] = {{-150, 1, -150}, {-150, 1, 150}, {150, 1, -150}, {150, 1, 150},
                           {-8, 1, -8},    {-8, 1, 8},    {8, 1, -8},    {8, 1, 8}};
  for (int i = 0; i < 8; i++) {
    fill_box(&ws->prims[i], pos[i][0], pos[i][1], pos[i][2], 0.8f, 0.8f, 0.8f);
  }
  ws->prim_count = 8;
}

static void run_lazy(NgRcWsCtx *ws, const int *vis, int n, uint32_t *frame, int frames, int ck,
                     int sk) {
  for (int i = 0; i < frames; i++) {
    ng_rc_ws_probe_lazy_tick(ws, vis, n, (*frame)++, ck, sk);
  }
}

/** Assert nk==1 parent refines under lazy split. */
static int test_nk1_split(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 64)) {
    fprintf(stderr, "FAIL nk1: cpu_begin\n");
    return 0;
  }
  fill_box(&ws.prims[0], 0.0f, 1.0f, 0.0f, 0.2f, 2.0f, 0.2f);
  ws.prim_count = 1;
  const int vis[1] = {0};
  const int cover_lod = NG_RC_WS_LOD_MAX - 2;
  {
    const float cell = ng_rc_ws_cell_size(cover_lod);
    const int32_t ix = (int32_t)floorf(0.0f / cell);
    const int32_t iy = (int32_t)floorf(1.0f / cell);
    const int32_t iz = (int32_t)floorf(0.0f / cell);
    if (ng_rc_ws_probe_insert_cell(&ws, (uint8_t)cover_lod, ix, iy, iz) < 0) {
      fprintf(stderr, "FAIL nk1: insert cover cell\n");
      ng_rc_ws_shutdown(&ws);
      return 0;
    }
  }
  const int used0 = ng_rc_ws_probe_used(&ws);
  int refined = 0;
  for (uint32_t f = 0; f < 128; f++) {
    ng_rc_ws_probe_lazy_tick(&ws, vis, 1, f, 0, 16);
    if (ng_rc_ws_probe_lod_min(&ws) < cover_lod) {
      refined = 1;
      break;
    }
  }
  char hist[256];
  ng_rc_ws_probe_hist_text(&ws, hist, sizeof(hist));
  if (!refined) {
    fprintf(stderr, "FAIL nk1: never refined below L%d (%s) used0=%d\n", cover_lod, hist, used0);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  printf("PASS nk1: %s\n", hist);
  ng_rc_ws_shutdown(&ws);
  return 1;
}

/** After split, cover must not reinsert parent while descendant exists. */
static int test_no_parent_refill(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 128)) {
    fprintf(stderr, "FAIL refill: cpu_begin\n");
    return 0;
  }
  fill_box(&ws.prims[0], 2.0f, 1.0f, 2.0f, 1.5f, 1.0f, 1.5f);
  ws.prim_count = 1;
  const int vis[1] = {0};
  for (uint32_t f = 0; f < 48; f++) {
    ng_rc_ws_probe_lazy_tick(&ws, vis, 1, f, 16, 16);
  }
  const int cover_lod = NG_RC_WS_LOD_MAX - 2;
  int fine_si = -1;
  for (int i = 0; i < ws.slot_cap; i++) {
    if (ws.slots[i].used && (int)ws.slots[i].lod < cover_lod) {
      fine_si = i;
      break;
    }
  }
  if (fine_si < 0) {
    fprintf(stderr, "FAIL refill: no fine slot to test\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  const int d = cover_lod - (int)ws.slots[fine_si].lod;
  const int32_t pix = ws.slots[fine_si].ix / (1 << d);
  const int32_t piy = ws.slots[fine_si].iy / (1 << d);
  const int32_t piz = ws.slots[fine_si].iz / (1 << d);
  if (!ng_rc_ws_probe_octant_occupied(&ws, cover_lod, pix, piy, piz)) {
    fprintf(stderr, "FAIL refill: octant not occupied\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  int had_parent = 0;
  for (int i = 0; i < ws.slot_cap; i++) {
    if (!ws.slots[i].used || (int)ws.slots[i].lod != cover_lod) {
      continue;
    }
    if (ws.slots[i].ix == pix && ws.slots[i].iy == piy && ws.slots[i].iz == piz) {
      had_parent = 1;
    }
  }
  for (uint32_t f = 0; f < 8; f++) {
    ng_rc_ws_probe_lazy_tick(&ws, vis, 1, 100 + f, 16, 0);
  }
  int has_parent = 0;
  for (int i = 0; i < ws.slot_cap; i++) {
    if (!ws.slots[i].used || (int)ws.slots[i].lod != cover_lod) {
      continue;
    }
    if (ws.slots[i].ix == pix && ws.slots[i].iy == piy && ws.slots[i].iz == piz) {
      has_parent = 1;
    }
  }
  if (!had_parent && has_parent) {
    fprintf(stderr, "FAIL refill: parent key reinserted with descendant\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  if (!ng_rc_ws_probe_octant_occupied(&ws, cover_lod, pix, piy, piz)) {
    fprintf(stderr, "FAIL refill: octant lost after cover ticks\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  printf("PASS refill: descendant occupies parent octant\n");
  ng_rc_ws_shutdown(&ws);
  return 1;
}

static int test_cull_out(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 512)) {
    fprintf(stderr, "FAIL cull_out: begin\n");
    return 0;
  }
  fill_scene8(&ws);
  int vis8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  int vis4[4] = {0, 1, 2, 3};
  uint32_t frame = 0;
  run_lazy(&ws, vis8, 8, &frame, 32, 16, 16);
  const int used0 = ng_rc_ws_probe_used(&ws);
  int on_b0 = 0;
  for (int pi = 4; pi < 8; pi++) {
    on_b0 += ng_rc_ws_probe_slots_on_prim(&ws, pi);
  }
  /* Release only (no refill) so used drop is observable. */
  run_lazy(&ws, vis4, 4, &frame, 16, 0, 0);
  const int used1 = ng_rc_ws_probe_used(&ws);
  int on_b1 = 0;
  for (int pi = 4; pi < 8; pi++) {
    on_b1 += ng_rc_ws_probe_slots_on_prim(&ws, pi);
  }
  if (on_b0 > 0 && on_b1 > 0) {
    // agent: composer-2.5 | 2026-08-12 | cull_out assert culled coverage | 976138
    fprintf(stderr, "FAIL cull_out: culled prims still covered (%d → %d slots) used %d→%d\n", on_b0,
            on_b1, used0, used1);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  char hist[256];
  ng_rc_ws_probe_hist_text(&ws, hist, sizeof(hist));
  printf("PASS cull_out: %s\n", hist);
  ng_rc_ws_shutdown(&ws);
  return 1;
}

static int test_cull_in(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 512)) {
    fprintf(stderr, "FAIL cull_in: begin\n");
    return 0;
  }
  fill_scene8(&ws);
  int vis4[4] = {0, 1, 2, 3};
  int vis8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  uint32_t frame = 0;
  const int budget = ws.slot_cap / 2;
  run_lazy(&ws, vis4, 4, &frame, 32, 16, 16);
  run_lazy(&ws, vis8, 8, &frame, 64, 16, 16);
  for (int pi = 4; pi < 8; pi++) {
    if (!ng_rc_ws_probe_prim_cover_ok(&ws, pi)) {
      fprintf(stderr, "FAIL cull_in: prim%d not covered\n", pi);
      ng_rc_ws_shutdown(&ws);
      return 0;
    }
  }
  if (ng_rc_ws_probe_used(&ws) > budget) {
    fprintf(stderr, "FAIL cull_in: used>%d\n", budget);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  char hist[256];
  ng_rc_ws_probe_hist_text(&ws, hist, sizeof(hist));
  printf("PASS cull_in: %s\n", hist);
  ng_rc_ws_shutdown(&ws);
  return 1;
}

static int test_pan_swap(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 512)) {
    fprintf(stderr, "FAIL pan_swap: begin\n");
    return 0;
  }
  fill_scene8(&ws);
  int A[4] = {0, 1, 2, 3};
  int B[4] = {4, 5, 6, 7};
  uint32_t frame = 0;
  const int cover_lod = NG_RC_WS_LOD_MAX - 2;
  run_lazy(&ws, A, 4, &frame, 48, 16, 16);
  run_lazy(&ws, B, 4, &frame, 64, 16, 16);
  for (int pi = 0; pi < 4; pi++) {
    if (ng_rc_ws_probe_slots_on_prim(&ws, pi) != 0) {
      fprintf(stderr, "FAIL pan_swap: A prim%d still covered\n", pi);
      ng_rc_ws_shutdown(&ws);
      return 0;
    }
  }
  for (int pi = 4; pi < 8; pi++) {
    if (!ng_rc_ws_probe_prim_cover_ok(&ws, pi)) {
      fprintf(stderr, "FAIL pan_swap: B prim%d not covered\n", pi);
      ng_rc_ws_shutdown(&ws);
      return 0;
    }
  }
  if (ng_rc_ws_probe_lod_min(&ws) >= cover_lod) {
    fprintf(stderr, "FAIL pan_swap: min_lod not refined\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  char hist[256];
  ng_rc_ws_probe_hist_text(&ws, hist, sizeof(hist));
  printf("PASS pan_swap: %s\n", hist);
  ng_rc_ws_shutdown(&ws);
  return 1;
}

static int test_steal_over(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 64)) {
    fprintf(stderr, "FAIL steal_over: begin\n");
    return 0;
  }
  fill_box(&ws.prims[0], 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f);
  ws.prim_count = 1;
  const int vis[1] = {0};
  const int budget = ws.slot_cap / 2;
  /* Direct slot fill: many fine + one coarse (bypass covering-scan insert). */
  int nfine = budget + 8;
  if (nfine > ws.slot_cap - 1) {
    nfine = ws.slot_cap - 1;
  }
  for (int i = 0; i < nfine; i++) {
    ws.slots[i].used = 1;
    ws.slots[i].dirty = 0;
    ws.slots[i].lod = 1;
    ws.slots[i].ix = (int32_t)(i * 3);
    ws.slots[i].iy = 0;
    ws.slots[i].iz = 100 + i;
  }
  ws.slots[nfine].used = 1;
  ws.slots[nfine].dirty = 0;
  ws.slots[nfine].lod = 5;
  ws.slots[nfine].ix = 0;
  ws.slots[nfine].iy = 0;
  ws.slots[nfine].iz = 0;
  ws.slot_count = nfine + 1;
  if (ng_rc_ws_probe_used(&ws) <= budget) {
    fprintf(stderr, "FAIL steal_over: could not exceed budget used=%d\n", ng_rc_ws_probe_used(&ws));
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  int coarse_before = 0;
  for (int i = 0; i < ws.slot_cap; i++) {
    if (ws.slots[i].used && (int)ws.slots[i].lod > 2) {
      coarse_before++;
    }
  }
  ng_rc_ws_probe_lazy_tick(&ws, vis, 1, 1, 0, 16);
  int coarse_after = 0;
  for (int i = 0; i < ws.slot_cap; i++) {
    if (ws.slots[i].used && (int)ws.slots[i].lod > 2) {
      coarse_after++;
    }
  }
  if (coarse_after < 1 || coarse_after < coarse_before) {
    fprintf(stderr, "FAIL steal_over: coarse dropped (%d→%d)\n", coarse_before, coarse_after);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  if (ng_rc_ws_probe_used(&ws) > budget) {
    fprintf(stderr, "FAIL steal_over: still over budget used=%d\n", ng_rc_ws_probe_used(&ws));
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  char hist[256];
  ng_rc_ws_probe_hist_text(&ws, hist, sizeof(hist));
  printf("PASS steal_over: %s\n", hist);
  ng_rc_ws_shutdown(&ws);
  return 1;
}

static int test_playground_refine(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 512)) {
    fprintf(stderr, "FAIL playground: begin\n");
    return 0;
  }
  fill_playground8(&ws);
  int vis[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  uint32_t frame = 0;
  const int cover_lod = NG_RC_WS_LOD_MAX - 2;
  const int budget = ws.slot_cap / 2;
  /* Cover-only first tick: expect ~8 coarse cells (the live-scene failure mode). */
  ng_rc_ws_probe_lazy_tick(&ws, vis, 8, frame++, 64, 0);
  const int used0 = ng_rc_ws_probe_used(&ws);
  if (used0 < 8) {
    fprintf(stderr, "FAIL playground: used0=%d expected >=8\n", used0);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  run_lazy(&ws, vis, 8, &frame, 128, 64, 16);
  const int used = ng_rc_ws_probe_used(&ws);
  const int lmin = ng_rc_ws_probe_lod_min(&ws);
  char hist[256];
  ng_rc_ws_probe_hist_text(&ws, hist, sizeof(hist));
  if (used <= 8) {
    fprintf(stderr, "FAIL playground: stuck at %d coarse (%s)\n", used, hist);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  if (lmin >= cover_lod) {
    fprintf(stderr, "FAIL playground: min_lod=%d not refined (%s)\n", lmin, hist);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  if (ng_rc_ws_probe_cover_unmet(&ws, vis, 8)) {
    fprintf(stderr, "FAIL playground: unmet cover (%s)\n", hist);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  if (used > budget) {
    fprintf(stderr, "FAIL playground: used=%d > budget=%d\n", used, budget);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  printf("PASS playground: %s\n", hist);
  ng_rc_ws_shutdown(&ws);
  return 1;
}

static int test_budget_swap(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 512)) {
    fprintf(stderr, "FAIL budget_swap: begin\n");
    return 0;
  }
  fill_scene8(&ws);
  int A[4] = {0, 1, 2, 3};
  int B[4] = {4, 5, 6, 7};
  uint32_t frame = 0;
  const int budget = ws.slot_cap / 2;
  run_lazy(&ws, A, 4, &frame, 96, 64, 16);
  if (!ng_rc_ws_probe_prim_cover_ok(&ws, 0) || ng_rc_ws_probe_cover_unmet(&ws, A, 4)) {
    fprintf(stderr, "FAIL budget_swap: A not fully covered used=%d\n", ng_rc_ws_probe_used(&ws));
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  run_lazy(&ws, B, 4, &frame, 128, 64, 16);
  for (int pi = 0; pi < 4; pi++) {
    if (ng_rc_ws_probe_slots_on_prim(&ws, pi) != 0) {
      fprintf(stderr, "FAIL budget_swap: A prim%d still covered\n", pi);
      ng_rc_ws_shutdown(&ws);
      return 0;
    }
  }
  for (int pi = 4; pi < 8; pi++) {
    if (!ng_rc_ws_probe_prim_cover_ok(&ws, pi)) {
      fprintf(stderr, "FAIL budget_swap: B prim%d not covered\n", pi);
      ng_rc_ws_shutdown(&ws);
      return 0;
    }
  }
  if (ng_rc_ws_probe_cover_unmet(&ws, B, 4)) {
    fprintf(stderr, "FAIL budget_swap: B still unmet\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  if (ng_rc_ws_probe_used(&ws) > budget) {
    fprintf(stderr, "FAIL budget_swap: used>%d\n", budget);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  char hist[256];
  ng_rc_ws_probe_hist_text(&ws, hist, sizeof(hist));
  printf("PASS budget_swap: %s\n", hist);
  ng_rc_ws_shutdown(&ws);
  return 1;
}

static int test_full_cover(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 512)) {
    fprintf(stderr, "FAIL full_cover: begin\n");
    return 0;
  }
  fill_scene8(&ws);
  int vis[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  uint32_t frame = 0;
  run_lazy(&ws, vis, 8, &frame, 128, 64, 16);
  if (ng_rc_ws_probe_cover_unmet(&ws, vis, 8)) {
    fprintf(stderr, "FAIL full_cover: unmet after drain\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  for (int pi = 0; pi < 8; pi++) {
    if (!ng_rc_ws_probe_prim_cover_ok(&ws, pi)) {
      fprintf(stderr, "FAIL full_cover: prim%d not covered\n", pi);
      ng_rc_ws_shutdown(&ws);
      return 0;
    }
  }
  char hist[256];
  ng_rc_ws_probe_hist_text(&ws, hist, sizeof(hist));
  printf("PASS full_cover: %s\n", hist);
  ng_rc_ws_shutdown(&ws);
  return 1;
}

static int test_persist(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 512)) {
    fprintf(stderr, "FAIL persist: begin\n");
    return 0;
  }
  fill_scene8(&ws);
  int vis[4] = {4, 5, 6, 7};
  uint32_t frame = 0;
  run_lazy(&ws, vis, 4, &frame, 96, 64, 16);
  const uint64_t fp0 = ng_rc_ws_probe_key_fp(&ws);
  for (int i = 0; i < 32; i++) {
    ng_rc_ws_probe_lazy_tick(&ws, vis, 4, frame++, 0, 0); /* idle: no cover/split */
  }
  const uint64_t fp1 = ng_rc_ws_probe_key_fp(&ws);
  if (fp0 != fp1) {
    fprintf(stderr, "FAIL persist: key fp changed %llu → %llu\n", (unsigned long long)fp0,
            (unsigned long long)fp1);
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  if (ng_rc_ws_probe_cover_unmet(&ws, vis, 4)) {
    fprintf(stderr, "FAIL persist: unmet after settle\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  printf("PASS persist: fp=%llu used=%d\n", (unsigned long long)fp0, ng_rc_ws_probe_used(&ws));
  ng_rc_ws_shutdown(&ws);
  return 1;
}

static int test_relax_stuck(void) {
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 128)) {
    fprintf(stderr, "FAIL relax_stuck: begin\n");
    return 0;
  }
  /* Two far prims; fill budget on A then need B. */
  fill_box(&ws.prims[0], -40.0f, 1.0f, 0.0f, 1.5f, 1.5f, 1.5f);
  fill_box(&ws.prims[1], 40.0f, 1.0f, 0.0f, 1.5f, 1.5f, 1.5f);
  ws.prim_count = 2;
  int A[1] = {0};
  int both[2] = {0, 1};
  uint32_t frame = 0;
  const int budget = ws.slot_cap / 2;
  run_lazy(&ws, A, 1, &frame, 80, 64, 16);
  /* Force near-full with fine cells if under budget. */
  while (ng_rc_ws_probe_used(&ws) < budget) {
    int placed = 0;
    for (int i = 0; i < ws.slot_cap && ng_rc_ws_probe_used(&ws) < budget; i++) {
      if (ws.slots[i].used) {
        continue;
      }
      ws.slots[i].used = 1;
      ws.slots[i].lod = 1;
      ws.slots[i].ix = (int32_t)(1000 + i);
      ws.slots[i].iy = 0;
      ws.slots[i].iz = 0;
      placed = 1;
      break;
    }
    if (!placed) {
      break;
    }
  }
  const int coarse_before = ng_rc_ws_probe_lod_max(&ws);
  run_lazy(&ws, both, 2, &frame, 64, 64, 16);
  if (!ng_rc_ws_probe_prim_cover_ok(&ws, 1)) {
    fprintf(stderr, "FAIL relax_stuck: prim1 not covered\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  /* Stable coarse on A side should still exist or parent cover after relax. */
  if (!ng_rc_ws_probe_prim_cover_ok(&ws, 0)) {
    fprintf(stderr, "FAIL relax_stuck: prim0 lost coverage\n");
    ng_rc_ws_shutdown(&ws);
    return 0;
  }
  (void)coarse_before;
  char hist[256];
  ng_rc_ws_probe_hist_text(&ws, hist, sizeof(hist));
  printf("PASS relax_stuck: %s\n", hist);
  ng_rc_ws_shutdown(&ws);
  return 1;
}

int main(void) {
  int fails = 0;
  NgRcWsCtx ws;
  if (!ng_rc_ws_probe_cpu_begin(&ws, 512)) {
    fprintf(stderr, "FAIL: cpu_begin\n");
    return 1;
  }
  fill_scene8(&ws);
  int vis[8];
  for (int i = 0; i < 8; i++) {
    vis[i] = i;
  }

  /* --- Full surface_tick golden --- */
  {
    NgRcWsCtx full;
    if (!ng_rc_ws_probe_cpu_begin(&full, 512)) {
      fprintf(stderr, "FAIL full: begin\n");
      return 1;
    }
    memcpy(full.prims, ws.prims, sizeof(ws.prims));
    full.prim_count = 8;
    ng_rc_ws_surface_tick(&full, vis, 8);
    const int used = ng_rc_ws_probe_used(&full);
    const int lmin = ng_rc_ws_probe_lod_min(&full);
    const int cover_lod = NG_RC_WS_LOD_MAX - 2;
    char hist[256];
    ng_rc_ws_probe_hist_text(&full, hist, sizeof(hist));
    printf("full: %s\n", hist);
    if (used <= 8) {
      fprintf(stderr, "FAIL full: used=%d expected >8\n", used);
      fails++;
    }
    if (lmin >= cover_lod) {
      fprintf(stderr, "FAIL full: min_lod=%d expected <%d\n", lmin, cover_lod);
      fails++;
    }
    ng_rc_ws_shutdown(&full);
  }

  /* --- Lazy tick --- */
  {
    NgRcWsCtx lazy;
    if (!ng_rc_ws_probe_cpu_begin(&lazy, 512)) {
      fprintf(stderr, "FAIL lazy: begin\n");
      return 1;
    }
    memcpy(lazy.prims, ws.prims, sizeof(ws.prims));
    lazy.prim_count = 8;
    const int cover_lod = NG_RC_WS_LOD_MAX - 2;
    const int budget = lazy.slot_cap / 2;
    for (uint32_t f = 0; f < 64; f++) {
      ng_rc_ws_probe_lazy_tick(&lazy, vis, 8, f, 16, 16);
    }
    const int used = ng_rc_ws_probe_used(&lazy);
    const int lmin = ng_rc_ws_probe_lod_min(&lazy);
    const int lmax = ng_rc_ws_probe_lod_max(&lazy);
    char hist[256];
    ng_rc_ws_probe_hist_text(&lazy, hist, sizeof(hist));
    printf("lazy: %s\n", hist);
    if (lmin >= cover_lod) {
      fprintf(stderr, "FAIL lazy: min_lod=%d expected <%d\n", lmin, cover_lod);
      fails++;
    }
    if (lmax < 0 || lmin == lmax) {
      fprintf(stderr, "FAIL lazy: need multiple lods (min=%d max=%d)\n", lmin, lmax);
      fails++;
    }
    if (used > budget) {
      fprintf(stderr, "FAIL lazy: used=%d > budget=%d\n", used, budget);
      fails++;
    }
    ng_rc_ws_shutdown(&lazy);
  }

  if (!test_nk1_split()) {
    fails++;
  }
  if (!test_no_parent_refill()) {
    fails++;
  }
  if (!test_cull_out()) {
    fails++;
  }
  if (!test_cull_in()) {
    fails++;
  }
  if (!test_pan_swap()) {
    fails++;
  }
  if (!test_steal_over()) {
    fails++;
  }
  if (!test_playground_refine()) {
    fails++;
  }
  if (!test_budget_swap()) {
    fails++;
  }
  if (!test_full_cover()) {
    fails++;
  }
  if (!test_persist()) {
    fails++;
  }
  if (!test_relax_stuck()) {
    fails++;
  }

  ng_rc_ws_shutdown(&ws);
  if (fails) {
    fprintf(stderr, "rc_ws_probe_smoke: %d FAIL\n", fails);
    return 1;
  }
  printf("rc_ws_probe_smoke: PASS\n");
  return 0;
}
/* agent: grok-4.6 | 2026-08-12 | rc_ws probe smoke tool | 585808 */
/* agent: grok-4.6 | 2026-08-12 | vis-change smoke scenarios | 0fd409 */
/* agent: grok-4.6 | 2026-08-12 | persist smoke cases | d54fbc */
/* agent: grok-4.6 | 2026-08-12 | playground 8 refine gate | 25a52f */
/* agent: composer-2.5 | 2026-08-12 | cull_out scene separation | 72e8d7 */
/* agent: composer-2.5 | 2026-08-12 | cull_out assert culled coverage | 976138 */
