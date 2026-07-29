// agent: composer-2.5 | 2026-07-28 | shared server tick runtime | 776fad
#ifndef NG_SERVER_RUNTIME_H
#define NG_SERVER_RUNTIME_H

#include <stdbool.h>

void ng_server_runtime_init(void);
void ng_server_runtime_poll_net(void);
void ng_server_runtime_poll_agent(void);
void ng_server_runtime_frame(float dt);
void ng_server_runtime_shutdown(void);

#endif
