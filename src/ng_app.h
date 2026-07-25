// agent: composer-2.5 | 2026-07-25 | app lifecycle and frame | 4c1f8a
#ifndef NG_APP_H
#define NG_APP_H

void ng_app_init(void);
void ng_app_frame(void);
void ng_app_shutdown(void);
void ng_app_cli_line(const char *line);

#endif
