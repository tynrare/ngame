// agent: composer-2.5 | 2026-07-25 | C command dispatch table | 1f7c3b
#ifndef NG_CLI_H
#define NG_CLI_H

void ng_cli_dispatch(int argc, const char **argv);
const char *ng_cli_feedback(int argc, const char **argv);
const char *ng_cli_last_output(void);

#endif
