// agent: composer-2.5 | 2026-07-25 | headless server orchestrator | j3m51h
#ifndef NG_APP_SERVER_H
#define NG_APP_SERVER_H

#include <stdbool.h>

void ng_app_server_init(int argc, char **argv);
void ng_app_server_frame(void);
void ng_app_server_shutdown(void);
bool ng_app_server_running(void);

#endif
