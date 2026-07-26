// agent: composer-2.5 | 2026-07-25 | mod_net embedded dual net | d02295
#ifndef MOD_NET_H
#define MOD_NET_H

#include "core/ng_mod.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

const NgModOps *mod_net_ops(void);
void *mod_net_ctx(void);
void mod_net_configure(const char *host, uint16_t port);
bool mod_net_is_connected(void);
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
bool mod_net_has_clients(void);
#endif
#if defined(NG_SERVER)
void mod_net_server_poll(void);
#endif
void mod_net_endpoint(char *host, size_t host_cap, uint16_t *port);
double mod_net_connect_elapsed(void);
#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
void mod_net_poll_recv(void);
void mod_net_flush_scene_updates(void);
#endif
#if defined(NG_HAS_EMBEDDED)
void mod_net_set_embedded(bool embedded);
void mod_net_set_local_loopback(bool local);
bool mod_net_is_embedded(void);
bool mod_net_is_local_loopback(void);
#endif

#endif
