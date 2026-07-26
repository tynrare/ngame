// agent: composer-2.5 | 2026-07-26 | entity sync mode enum | a1b2c3
#ifndef NG_SYNC_H
#define NG_SYNC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum NgSyncMode {
  NG_SYNC_SERVER = 0,
  NG_SYNC_SHARED = 1,
  NG_SYNC_OWNER = 2,
  NG_SYNC_LOCAL = 3,
} NgSyncMode;

const char *ng_sync_mode_name(NgSyncMode mode);
NgSyncMode ng_sync_mode_parse(const char *s);
bool ng_sync_runs_on_client(NgSyncMode mode);
bool ng_sync_runs_on_server(NgSyncMode mode);
bool ng_sync_posts_wire(NgSyncMode mode);

#endif
