<!-- agent: composer-2.5 | 2026-08-02 | infra-retry warm soak harness | f222cb -->
# Throttle / ping / loss matrix

Highest step that still passed product criteria. `(desync%)` = hash-mismatch sample share. `INFRA@` = launch/settle exhausted (not a product limit). `—` = baseline product fail.

## Highest values that still work

### 1 client

| scene | input | ping | loss | throttle | combined | valid |
|-------|-------|------|------|----------|----------|-------|
| `stacking` | F | — | — | — | — | yes |
| `physics` | W/A/S/D | 500 ms (0%) | 90% (0%) | 90% (0%) | 500/60/90 (0%) | ? |
| `stress_spawn` | W/S | 500 ms (0%) | 90% (0%) | 90% (0%) | 500/60/90 (0%) | ? |
| `stress_spawn_idle` | idle | 500 ms (0%) | 90% (0%) | 90% (0%) | 500/60/90 (0%) | ? |

### 2 clients

| scene | input | ping | loss | throttle | combined | valid |
|-------|-------|------|------|----------|----------|-------|
| `stacking` | F | 0 ms (0%) | 90% (0%) | 20% (0%) | 1000/70/90 (0%) | yes |
| `physics` | W/A/S/D | 200 ms (0%) | 90% (0%) | 90% (0%) | 1000/70/90 (0%) | ? |
| `stress_spawn` | W/S | — | — | 0% (36%) | — | ? |
| `stress_spawn_idle` | idle | — | — | 10% (6%) | 50/10/20 (0%) | ? |

## Method

| | |
|--|--|
| Soak / warm / sample | **8s** / **2s** / **0.5s** |
| Stop | desync≥**90%** (full soak), unrecoverable, ceiling **5000ms**/**90%**/**90%** |
| Infra | retry **3×**; not a product limit |
| Rule | `.cursor/rules/throttle-soak.mdc` |

## stacking — limit detail

| clients | ping-only | loss-only | throttle-only | combined |
|--------:|-----------|-----------|---------------|----------|
| 1 | — | — | — | — |
| 2 | OK≤0 ms (0%) → FAIL@25 ms (0%) `tick_frozen` | OK≤90% (0%) | OK≤20% (0%) → FAIL@30% (100%) `phys_resync_exhausted` | OK≤1000/70/90 (0%) → FAIL@2000/80/90 (0%) `tick_frozen` |

### stacking / 2-client — step grid

**ping (ms)**

| 0 | 25 |
|---:|---:|
| OK 0% | X:tick_frozen 0% |

**loss (%)**

| 0 | 5 | 10 | 15 | 20 | 25 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% |

**throttle (%)**

| 0 | 10 | 20 | 30 |
|---:|---:|---:|---:|
| OK 0% | OK 27% | OK 0% | X:phys_resync_ 100% |

**combined**

| 0/0/0 | 25/5/10 | 50/10/20 | 75/15/30 | 100/20/40 | 150/25/50 | 200/30/60 | 300/40/70 | 400/50/80 | 500/60/90 | 1000/70/90 | 2000/80/90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 53% | OK 73% | OK 50% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | X:tick_frozen 0% |

## physics — limit detail

| clients | ping-only | loss-only | throttle-only | combined |
|--------:|-----------|-----------|---------------|----------|
| 1 | OK≤500 ms (0%) → FAIL@750 ms (0%) `clients_not_ready` | OK≤90% (0%) | OK≤90% (0%) | OK≤500/60/90 (0%) → FAIL@1000/70/90 (0%) `clients_not_ready` |
| 2 | OK≤200 ms (0%) → FAIL@300 ms (0%) `clients_not_ready` | OK≤90% (0%) | OK≤90% (0%) | OK≤1000/70/90 (0%) → FAIL@2000/80/90 (0%) `clients_not_ready` |

### physics / 1-client — step grid

**ping (ms)**

| 0 | 25 | 50 | 75 | 100 | 150 | 200 | 300 | 400 | 500 | 750 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | X:clients_not_ 0% |

**loss (%)**

| 0 | 5 | 10 | 15 | 20 | 25 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% |

**throttle (%)**

| 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% |

**combined**

| 0/0/0 | 25/5/10 | 50/10/20 | 75/15/30 | 100/20/40 | 150/25/50 | 200/30/60 | 300/40/70 | 400/50/80 | 500/60/90 | 1000/70/90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | X:clients_not_ 0% |

### physics / 2-client — step grid

**ping (ms)**

| 0 | 25 | 50 | 75 | 100 | 150 | 200 | 300 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | X:clients_not_ 0% |

**loss (%)**

| 0 | 5 | 10 | 15 | 20 | 25 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% |

**throttle (%)**

| 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 29% | OK 29% | OK 29% | OK 43% | OK 0% | OK 0% | OK 50% | OK 0% | OK 0% |

**combined**

| 0/0/0 | 25/5/10 | 50/10/20 | 75/15/30 | 100/20/40 | 150/25/50 | 200/30/60 | 300/40/70 | 400/50/80 | 500/60/90 | 1000/70/90 | 2000/80/90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | X:clients_not_ 0% |

## stress_spawn — limit detail

| clients | ping-only | loss-only | throttle-only | combined |
|--------:|-----------|-----------|---------------|----------|
| 1 | OK≤500 ms (0%) → FAIL@750 ms (0%) `clients_not_ready` | OK≤90% (0%) | OK≤90% (0%) | OK≤500/60/90 (0%) → FAIL@1000/70/90 (0%) `clients_not_ready` |
| 2 | FAIL@0 ms (80%) `phys_resync_exhausted` | FAIL@0% (67%) `phys_resync_exhausted` | OK≤0% (36%) → FAIL@10% (38%) `phys_resync_exhausted` | FAIL@0/0/0 (71%) `phys_resync_exhausted` |

### stress_spawn / 1-client — step grid

**ping (ms)**

| 0 | 25 | 50 | 75 | 100 | 150 | 200 | 300 | 400 | 500 | 750 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | X:clients_not_ 0% |

**loss (%)**

| 0 | 5 | 10 | 15 | 20 | 25 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% |

**throttle (%)**

| 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% |

**combined**

| 0/0/0 | 25/5/10 | 50/10/20 | 75/15/30 | 100/20/40 | 150/25/50 | 200/30/60 | 300/40/70 | 400/50/80 | 500/60/90 | 1000/70/90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | X:clients_not_ 0% |

### stress_spawn / 2-client — step grid

**ping (ms)**

| 0 |
|---:|
| X:phys_resync_ 80% |

**loss (%)**

| 0 |
|---:|
| X:phys_resync_ 67% |

**throttle (%)**

| 0 | 10 |
|---:|---:|
| OK 36% | X:phys_resync_ 38% |

**combined**

| 0/0/0 |
|---:|
| X:phys_resync_ 71% |

## stress_spawn_idle — limit detail

| clients | ping-only | loss-only | throttle-only | combined |
|--------:|-----------|-----------|---------------|----------|
| 1 | OK≤500 ms (0%) → FAIL@750 ms (0%) `clients_not_ready` | OK≤90% (0%) | OK≤90% (0%) | OK≤500/60/90 (0%) → FAIL@1000/70/90 (0%) `clients_not_ready` |
| 2 | FAIL@0 ms (100%) `desync_time` | FAIL@0% (23%) `phys_resync_exhausted` | OK≤10% (6%) → FAIL@20% (14%) `phys_resync_exhausted` | OK≤50/10/20 (0%) → FAIL@75/15/30 (0%) `clients_not_ready` |

### stress_spawn_idle / 1-client — step grid

**ping (ms)**

| 0 | 25 | 50 | 75 | 100 | 150 | 200 | 300 | 400 | 500 | 750 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | X:clients_not_ 0% |

**loss (%)**

| 0 | 5 | 10 | 15 | 20 | 25 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% |

**throttle (%)**

| 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% |

**combined**

| 0/0/0 | 25/5/10 | 50/10/20 | 75/15/30 | 100/20/40 | 150/25/50 | 200/30/60 | 300/40/70 | 400/50/80 | 500/60/90 | 1000/70/90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | OK 0% | X:clients_not_ 0% |

### stress_spawn_idle / 2-client — step grid

**ping (ms)**

| 0 |
|---:|
| X:desync_time 100% |

**loss (%)**

| 0 |
|---:|
| X:phys_resync_ 23% |

**throttle (%)**

| 0 | 10 | 20 |
|---:|---:|---:|
| OK 13% | OK 6% | X:phys_resync_ 14% |

**combined**

| 0/0/0 | 25/5/10 | 50/10/20 | 75/15/30 |
|---:|---:|---:|---:|
| OK 13% | OK 0% | OK 0% | X:clients_not_ 0% |

<!-- agent: composer-2.5 | 2026-08-02 | infra-retry warm soak harness | f222cb -->
