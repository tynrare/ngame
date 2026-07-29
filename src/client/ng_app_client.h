// agent: composer-2.5 | 2026-07-25 | client app orchestrator | i2l40g
#ifndef NG_APP_CLIENT_H
#define NG_APP_CLIENT_H

void ng_app_client_init(int argc, char **argv);
void ng_app_client_frame(void);
void ng_app_client_shutdown(void);
// agent: composer-2.5 | 2026-07-29 | expose launch mode text | 65f1a9
const char *ng_app_client_mode_text(void);

#endif
// agent: composer-2.5 | 2026-07-29 | expose launch mode text | 65f1a9
