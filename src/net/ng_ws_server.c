// agent: composer-2.5 | 2026-07-25 | websocket server bridge | m6p84k
#include "ng_ws_server.h"
#include "core/ng_log.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define NG_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define NG_WS_RX_MAX 65536

struct NgWsServer {
  int listen_fd;
  int client_fd;
  bool handshaked;
  uint8_t rx[NG_WS_RX_MAX];
  size_t rx_len;
};

static void ng_ws_set_nonblock(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

static void ng_ws_sha1(const uint8_t *msg, size_t len, uint8_t out[20]) {
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  const size_t new_len = ((len + 8) / 64 + 1) * 64;
  uint8_t *m = (uint8_t *)calloc(new_len, 1);
  memcpy(m, msg, len);
  m[len] = 0x80;
  const uint64_t bits = (uint64_t)len * 8u;
  for (int i = 0; i < 8; i++) {
    m[new_len - 1 - i] = (uint8_t)(bits >> (8 * i));
  }
  for (size_t off = 0; off < new_len; off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
      w[i] = ((uint32_t)m[off + i * 4] << 24) | ((uint32_t)m[off + i * 4 + 1] << 16) |
             ((uint32_t)m[off + i * 4 + 2] << 8) | (uint32_t)m[off + i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
      w[i] = ((w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]) << 1) |
             ((w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]) >> 31);
    }
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      const uint32_t temp = (((a << 5) | (a >> 27)) + f + e + k + w[i]);
      e = d;
      d = c;
      c = (b << 30) | (b >> 2);
      b = a;
      a = temp;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }
  free(m);
  uint32_t hs[5] = {h0, h1, h2, h3, h4};
  memcpy(out, hs, 20);
}

static void ng_ws_b64(const uint8_t *in, size_t in_len, char *out, size_t out_cap) {
  static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < in_len; i += 3) {
    const uint32_t n = ((uint32_t)in[i] << 16) |
                       ((i + 1 < in_len) ? ((uint32_t)in[i + 1] << 8) : 0) |
                       ((i + 2 < in_len) ? (uint32_t)in[i + 2] : 0);
    if (o + 4 >= out_cap) {
      break;
    }
    out[o++] = tbl[(n >> 18) & 63];
    out[o++] = tbl[(n >> 12) & 63];
    out[o++] = (i + 1 < in_len) ? tbl[(n >> 6) & 63] : '=';
    out[o++] = (i + 2 < in_len) ? tbl[n & 63] : '=';
  }
  out[o] = '\0';
}

// agent: composer-2.5 | 2026-07-25 | buffered ws handshake | 24182c
static bool ng_ws_handshake(NgWsServer *s) {
  char tmp[512];
  const ssize_t n = recv(s->client_fd, tmp, sizeof(tmp), 0);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return true;
  }
  if (n <= 0) {
    close(s->client_fd);
    s->client_fd = -1;
    s->rx_len = 0;
    return false;
  }
  if (s->rx_len + (size_t)n >= sizeof(s->rx)) {
    close(s->client_fd);
    s->client_fd = -1;
    s->rx_len = 0;
    return false;
  }
  memcpy(s->rx + s->rx_len, tmp, (size_t)n);
  s->rx_len += (size_t)n;
  s->rx[s->rx_len] = '\0';

  const char *end = strstr((const char *)s->rx, "\r\n\r\n");
  if (!end) {
    return true;
  }

  const char *key_hdr = strstr((const char *)s->rx, "Sec-WebSocket-Key:");
  if (!key_hdr || key_hdr >= end) {
    close(s->client_fd);
    s->client_fd = -1;
    s->rx_len = 0;
    return false;
  }
  key_hdr += 18;
  while (*key_hdr == ' ') {
    key_hdr++;
  }
  char key[128];
  int ki = 0;
  while (*key_hdr > ' ' && ki < (int)sizeof(key) - 1) {
    key[ki++] = *key_hdr++;
  }
  key[ki] = '\0';

  char accept_src[256];
  snprintf(accept_src, sizeof(accept_src), "%s%s", key, NG_WS_GUID);
  uint8_t digest[20];
  ng_ws_sha1((const uint8_t *)accept_src, strlen(accept_src), digest);
  char accept[64];
  ng_ws_b64(digest, 20, accept, sizeof(accept));

  char resp[512];
  snprintf(resp, sizeof(resp),
           "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: %s\r\n\r\n",
           accept);
  send(s->client_fd, resp, strlen(resp), 0);
  s->handshaked = true;
  s->rx_len = 0;
  NG_LOG_INFO("websocket client handshaked");
  return true;
}

NgWsServer *ng_ws_server_create(uint16_t port) {
  NgWsServer *s = (NgWsServer *)calloc(1, sizeof(NgWsServer));
  if (!s) {
    return NULL;
  }
  s->client_fd = -1;
  s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (s->listen_fd < 0) {
    free(s);
    return NULL;
  }
  const int yes = 1;
  setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  ng_ws_set_nonblock(s->listen_fd);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(s->listen_fd);
    free(s);
    return NULL;
  }
  listen(s->listen_fd, 4);
  NG_LOG_INFO("websocket server :%u", port);
  return s;
}

void ng_ws_server_destroy(NgWsServer *s) {
  if (!s) {
    return;
  }
  if (s->client_fd >= 0) {
    close(s->client_fd);
  }
  if (s->listen_fd >= 0) {
    close(s->listen_fd);
  }
  free(s);
}

bool ng_ws_server_connected(NgWsServer *s) { return s && s->handshaked; }

static bool ng_ws_read_frames(NgWsServer *s, NgWsPacketFn fn, void *ctx) {
  char tmp[4096];
  const ssize_t n = recv(s->client_fd, tmp, sizeof(tmp), 0);
  if (n <= 0) {
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;
    }
    close(s->client_fd);
    s->client_fd = -1;
    s->handshaked = false;
    s->rx_len = 0;
    return false;
  }
  if (s->rx_len + (size_t)n > NG_WS_RX_MAX) {
    s->rx_len = 0;
    return false;
  }
  memcpy(s->rx + s->rx_len, tmp, (size_t)n);
  s->rx_len += (size_t)n;

  while (s->rx_len >= 2) {
    const size_t hdr = 2;
    const uint8_t op = s->rx[0] & 0x0f;
    const bool masked = (s->rx[1] & 0x80) != 0;
    uint64_t plen = s->rx[1] & 0x7f;
    size_t pos = hdr;
    if (plen == 126) {
      if (s->rx_len < 4) {
        return true;
      }
      plen = ((uint64_t)s->rx[2] << 8) | s->rx[3];
      pos = 4;
    } else if (plen == 127) {
      if (s->rx_len < 10) {
        return true;
      }
      plen = 0;
      for (int i = 0; i < 8; i++) {
        plen = (plen << 8) | s->rx[2 + i];
      }
      pos = 10;
    }
    uint8_t mask[4] = {0};
    if (masked) {
      if (s->rx_len < pos + 4) {
        return true;
      }
      memcpy(mask, s->rx + pos, 4);
      pos += 4;
    }
    if (s->rx_len < pos + plen) {
      return true;
    }
    if (op == 0x2 && fn) {
      uint8_t payload[NG_WS_RX_MAX];
      if (plen > NG_WS_RX_MAX) {
        return false;
      }
      for (size_t i = 0; i < (size_t)plen; i++) {
        payload[i] = s->rx[pos + i] ^ (masked ? mask[i % 4] : 0);
      }
      fn(payload, (size_t)plen, ctx);
    }
    const size_t total = pos + (size_t)plen;
    memmove(s->rx, s->rx + total, s->rx_len - total);
    s->rx_len -= total;
  }
  return true;
}

bool ng_ws_server_poll(NgWsServer *s, NgWsPacketFn fn, void *ctx) {
  if (!s) {
    return false;
  }
  if (s->client_fd < 0) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    const int fd = accept(s->listen_fd, (struct sockaddr *)&addr, &len);
    if (fd >= 0) {
      ng_ws_set_nonblock(fd);
      s->client_fd = fd;
      s->handshaked = false;
      s->rx_len = 0;
    }
    if (s->client_fd < 0) {
      return true;
    }
  }
  if (!s->handshaked) {
    return ng_ws_handshake(s);
  }
  return ng_ws_read_frames(s, fn, ctx);
}

bool ng_ws_server_send(NgWsServer *s, const uint8_t *data, size_t len) {
  if (!s || !s->handshaked || !data || len == 0) {
    return false;
  }
  for (size_t off = 0; off < len;) {
    const size_t chunk = (len - off > 125) ? 125 : (len - off);
    uint8_t hdr[2 + 125];
    hdr[0] = 0x82;
    hdr[1] = (uint8_t)chunk;
    memcpy(hdr + 2, data + off, chunk);
    if (send(s->client_fd, hdr, 2 + chunk, 0) != (ssize_t)(2 + chunk)) {
      return false;
    }
    off += chunk;
  }
  return true;
}
