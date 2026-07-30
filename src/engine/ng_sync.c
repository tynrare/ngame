// agent: composer-2.5 | 2026-07-26 | sync mode parse helpers | d4e5f6
#include "ng_sync.h"
#include <string.h>

const char *ng_sync_mode_name(NgSyncMode mode) {
  switch (mode) {
  case NG_SYNC_SERVER:
    return "server";
  case NG_SYNC_SHARED:
    return "shared";
  case NG_SYNC_OWNER:
    return "owner";
  case NG_SYNC_LOCAL:
    return "local";
  default:
    return "server";
  }
}

NgSyncMode ng_sync_mode_parse(const char *s) {
  if (!s) {
    return NG_SYNC_SERVER;
  }
  if (strcmp(s, "shared") == 0) {
    return NG_SYNC_SHARED;
  }
  if (strcmp(s, "owner") == 0) {
    return NG_SYNC_OWNER;
  }
  if (strcmp(s, "local") == 0) {
    return NG_SYNC_LOCAL;
  }
  return NG_SYNC_SERVER;
}

bool ng_sync_runs_on_client(NgSyncMode mode) {
  return mode == NG_SYNC_SHARED || mode == NG_SYNC_OWNER || mode == NG_SYNC_LOCAL;
}

bool ng_sync_runs_on_server(NgSyncMode mode) { return mode == NG_SYNC_SERVER; }

// agent: composer-2.5 | 2026-07-30 | posts wire includes server | cad507
bool ng_sync_posts_wire(NgSyncMode mode) {
  /* server: host-authored physics / transforms stream via STATE_UPDATE. */
  return mode == NG_SYNC_SHARED || mode == NG_SYNC_OWNER || mode == NG_SYNC_SERVER;
}

// agent: composer-2.5 | 2026-07-26 | sync mode parse helpers | d4e5f6
// agent: composer-2.5 | 2026-07-30 | posts wire includes server | cad507
