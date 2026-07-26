// agent: composer-2.5 | 2026-07-25 | embedded app orchestrator | 680bc7
#ifndef NG_APP_EMBEDDED_H
#define NG_APP_EMBEDDED_H

#include <stdbool.h>

void ng_app_embedded_init(void);
void ng_app_embedded_frame(float dt);
void ng_app_embedded_shutdown(void);
bool ng_app_embedded_ready(void);

#endif
