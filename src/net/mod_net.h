// agent: composer-2.5 | 2026-07-27 | net flush and session broadcast | e8f9a0
// agent: composer-2.5 | 2026-07-28 | gateway loopback upstream API | 34cffe
#ifndef MOD_NET_H
#define MOD_NET_H

#include "engine/ng_mod.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NG_NET_LOOPBACK_PORT 27017
#define NG_AGENT_LOCAL_PORT 27101

const NgModOps *mod_net_ops(void);
void *mod_net_ctx(void);
void mod_net_configure(const char *host, uint16_t port);
bool mod_net_is_connected(void);
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
bool mod_net_has_clients(void);
void mod_net_broadcast_scene_session(void);
#endif
#if defined(NG_SERVER)
void mod_net_server_poll(void);
#endif
#if defined(NG_HAS_EMBEDDED)
void mod_net_set_gateway(bool gateway);
bool mod_net_is_gateway(void);
void mod_net_configure_upstream(const char *host, uint16_t port);
bool mod_net_upstream_connected(void);
bool mod_net_is_authoritative(void);
// agent: composer-2.5 | 2026-07-29 | upstream endpoint accessor | 0a7d3c
void mod_net_upstream_endpoint(char *host, size_t host_cap, uint16_t *port);
void mod_net_gateway_host_poll(void);
bool mod_net_gateway_upstream_cmd(const char *line, char *reply, size_t reply_cap);
void mod_net_gateway_resync(void);
void mod_net_gateway_sync_view(void);
void mod_net_gateway_status_text(char *out, size_t cap);
uint16_t mod_net_assigned_agent_port(void);
bool mod_net_skip_local_boot(void);
void mod_net_root_mirror_text(char *out, size_t cap);
#endif
void mod_net_endpoint(char *host, size_t host_cap, uint16_t *port);
double mod_net_connect_elapsed(void);
void mod_net_flush_scene_updates(void);
#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
void mod_net_poll_recv(void);
#endif

#endif
// agent: composer-2.5 | 2026-07-29 | upstream endpoint accessor | 0a7d3c
// agent: composer-2.5 | 2026-07-29 | gateway sync view helper | 124fc1
