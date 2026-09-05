#ifndef NG_SOCKET_H
#define NG_SOCKET_H

#include <stdbool.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET NgSocket;
#define NG_INVALID_SOCKET INVALID_SOCKET
static inline int ng_socket_error(void) { return WSAGetLastError(); }
static inline bool ng_socket_would_block(void) { return ng_socket_error() == WSAEWOULDBLOCK; }
static inline void ng_socket_close(NgSocket fd) { closesocket(fd); }
static inline void ng_socket_set_blocking(NgSocket fd, bool blocking) {
  u_long mode = blocking ? 0 : 1;
  ioctlsocket(fd, FIONBIO, &mode);
}
static inline bool ng_socket_startup(void) {
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}
static inline void ng_socket_cleanup(void) { WSACleanup(); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NgSocket;
#define NG_INVALID_SOCKET (-1)
static inline int ng_socket_error(void) { return errno; }
static inline bool ng_socket_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
static inline void ng_socket_close(NgSocket fd) { close(fd); }
static inline void ng_socket_set_blocking(NgSocket fd, bool blocking) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
}
static inline bool ng_socket_startup(void) { return true; }
static inline void ng_socket_cleanup(void) {}
#endif

#endif
