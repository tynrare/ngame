// agent: composer-2.5 | 2026-07-25 | net bus bridge module | f9i17d
#ifndef MOD_NET_H
#define MOD_NET_H

#include "core/ng_mod.h"

const NgModOps *mod_net_ops(void);
void *mod_net_ctx(void);
void mod_net_configure(const char *host, uint16_t port);
bool mod_net_is_connected(void);
#ifdef NG_SERVER
bool mod_net_has_clients(void);
#endif
void mod_net_endpoint(char *host, size_t host_cap, uint16_t *port);
double mod_net_connect_elapsed(void);

#endif
