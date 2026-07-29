// agent: composer-2.5 | 2026-07-25 | spawn port wait devnull | 3548df
// agent: composer-2.5 | 2026-07-28 | gateway agent upstream ports | 2cac03
// agent: composer-2.5 | 2026-07-29 | default local sets upstream | c2d01b
#include "ng_launch.h"
#include "engine/ng_log.h"
#include "net/mod_net.h"
#include "server/agent.h"
#include "net/ng_net.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#endif

static pid_t g_server_pid = 0;

static void ng_launch_parse_host_port(const char *spec, NgLaunchConfig *cfg) {
  if (!spec || !cfg) {
    return;
  }
  char spec_buf[128];
  strncpy(spec_buf, spec, sizeof(spec_buf) - 1);
  spec_buf[sizeof(spec_buf) - 1] = '\0';
  char *colon = strchr(spec_buf, ':');
  if (colon) {
    *colon = '\0';
    strncpy(cfg->host, spec_buf, sizeof(cfg->host) - 1);
    cfg->host[sizeof(cfg->host) - 1] = '\0';
    cfg->port = (uint16_t)atoi(colon + 1);
  } else {
    strncpy(cfg->host, spec_buf, sizeof(cfg->host) - 1);
    cfg->host[sizeof(cfg->host) - 1] = '\0';
  }
}

void ng_launch_print_usage(const char *prog) {
  const char *name = prog ? prog : "ngame";
  fprintf(stderr,
          "Usage: %s [mode] [options]\n"
          "\n"
          "Modes (native):\n"
          "  --local [--port PORT]       spawn ngame_server + gateway upstream\n"
          "  --remote HOST:PORT          gateway upstream to existing server\n"
          "  --solo                      local gateway only (no upstream root)\n"
          "\n"
          "Aliases:\n"
          "  --connect HOST:PORT         same as --remote\n"
          "  --embedded                  same as --solo\n"
          "\n"
          "Options:\n"
          "  --agent-port PORT           MCP port (solo default 27100; upstream uses root-assigned)\n"
          "\n"
          "Native default: --local (spawn ngame_server + gateway upstream)\n"
          "Web default: --solo\n",
          name);
}

bool ng_launch_parse(int argc, char **argv, NgLaunchConfig *cfg) {
  if (!cfg) {
    return false;
  }
  cfg->mode =
#if defined(__EMSCRIPTEN__)
      NG_LAUNCH_LOCAL;
#else
      NG_LAUNCH_LOCAL;
#endif
  strncpy(cfg->host, NG_NET_HOST, sizeof(cfg->host) - 1);
  cfg->host[sizeof(cfg->host) - 1] = '\0';
  cfg->port = NG_NET_DEFAULT_PORT;
  cfg->agent_port = NG_AGENT_DEFAULT_PORT;
  cfg->use_upstream = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      ng_launch_print_usage(argv[0]);
      return false;
    }
    if (strcmp(argv[i], "--solo") == 0 || strcmp(argv[i], "--embedded") == 0) {
      cfg->mode = NG_LAUNCH_SOLO;
    } else if (strcmp(argv[i], "--local") == 0) {
      cfg->mode = NG_LAUNCH_LOCAL;
      cfg->use_upstream = true;
    } else if (strcmp(argv[i], "--remote") == 0) {
      cfg->mode = NG_LAUNCH_REMOTE;
      cfg->use_upstream = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        ng_launch_parse_host_port(argv[++i], cfg);
      }
    } else if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
      cfg->mode = NG_LAUNCH_REMOTE;
      cfg->use_upstream = true;
      ng_launch_parse_host_port(argv[++i], cfg);
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      cfg->port = (uint16_t)atoi(argv[++i]);
    } else if (strcmp(argv[i], "--agent-port") == 0 && i + 1 < argc) {
      cfg->agent_port = (uint16_t)atoi(argv[++i]);
    }
  }

  if (cfg->mode == NG_LAUNCH_LOCAL || cfg->mode == NG_LAUNCH_REMOTE) {
    cfg->use_upstream = true;
  }

  return true;
}

#if defined(__linux__)
static bool ng_launch_exe_dir(char *buf, size_t cap) {
  char link[512];
  const ssize_t n = readlink("/proc/self/exe", link, sizeof(link) - 1);
  if (n <= 0) {
    return false;
  }
  link[n] = '\0';
  char *slash = strrchr(link, '/');
  if (!slash) {
    return false;
  }
  *slash = '\0';
  strncpy(buf, link, cap - 1);
  buf[cap - 1] = '\0';
  return true;
}

static bool ng_launch_server_path(char *buf, size_t cap) {
  char dir[512];
  if (!ng_launch_exe_dir(dir, sizeof(dir))) {
    return false;
  }
  snprintf(buf, cap, "%s/ngame_server", dir);
  struct stat st;
  return stat(buf, &st) == 0 && S_ISREG(st.st_mode);
}

static void ng_launch_kill_stale_server(uint16_t port) {
  (void)port;
  if (g_server_pid > 0) {
    return;
  }
  FILE *fp = popen("pidof ngame_server 2>/dev/null", "r");
  if (!fp) {
    return;
  }
  char buf[64];
  if (fgets(buf, sizeof(buf), fp)) {
    const pid_t pid = (pid_t)atoi(buf);
    if (pid > 0) {
      kill(pid, SIGKILL);
      waitpid(pid, NULL, 0);
    }
  }
  pclose(fp);
  usleep(200000);
}

static bool ng_launch_port_open(uint16_t port) {
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return false;
  }
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port ? port : NG_NET_DEFAULT_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  const int r = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
  const int err = errno;
  close(fd);
  return r != 0 && err == EADDRINUSE;
}

static bool ng_launch_wait_port(uint16_t port, pid_t pid, int timeout_ms) {
  for (int waited = 0; waited < timeout_ms; waited += 50) {
    if (waitpid(pid, NULL, WNOHANG) > 0) {
      return false;
    }
    if (ng_launch_port_open(port)) {
      return true;
    }
    usleep(50000);
  }
  return ng_launch_port_open(port) && waitpid(pid, NULL, WNOHANG) == 0;
}

static void ng_launch_detach_stdio(void) {
  const int nullfd = open("/dev/null", O_RDWR);
  if (nullfd < 0) {
    return;
  }
  dup2(nullfd, STDIN_FILENO);
  dup2(nullfd, STDOUT_FILENO);
  dup2(nullfd, STDERR_FILENO);
  if (nullfd > STDERR_FILENO) {
    close(nullfd);
  }
}
#endif

bool ng_launch_spawn_server(uint16_t port) {
#if defined(__linux__)
  if (g_server_pid > 0) {
    return ng_launch_port_open(port);
  }

  // agent: composer-2.5 | 2026-07-29 | reuse running server | a3c7e4
  if (ng_launch_port_open(port)) {
    return true;
  }

  ng_launch_kill_stale_server(port);

  char server_path[512];
  if (!ng_launch_server_path(server_path, sizeof(server_path))) {
    NG_LOG_ERROR("ngame_server not found next to executable");
    return false;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    ng_launch_detach_stdio();
    char port_arg[16];
    snprintf(port_arg, sizeof(port_arg), "%u", port ? port : NG_NET_DEFAULT_PORT);
    execl(server_path, "ngame_server", "--port", port_arg, (char *)NULL);
    _exit(1);
  }

  g_server_pid = pid;
  const bool ok = ng_launch_wait_port(port, pid, 5000);
  if (!ok) {
    NG_LOG_ERROR("ngame_server failed to start");
    ng_launch_stop_server();
    return false;
  }
  NG_LOG_INFO("local server pid %d", (int)g_server_pid);
  return true;
#else
  (void)port;
  return false;
#endif
}

void ng_launch_stop_server(void) {
#if defined(__linux__)
  if (g_server_pid <= 0) {
    return;
  }
  kill(g_server_pid, SIGTERM);
  for (int i = 0; i < 50; i++) {
    if (waitpid(g_server_pid, NULL, WNOHANG) > 0) {
      g_server_pid = 0;
      return;
    }
    usleep(100000);
  }
  kill(g_server_pid, SIGKILL);
  waitpid(g_server_pid, NULL, 0);
  g_server_pid = 0;
#endif
}

bool ng_launch_server_spawned(void) { return g_server_pid > 0; }

// agent: composer-2.5 | 2026-07-29 | reuse running server | a3c7e4
// agent: composer-2.5 | 2026-07-29 | default local sets upstream | c2d01b
// agent: composer-2.5 | 2026-07-29 | default local sets upstream | c2d01b
