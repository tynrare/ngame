// agent: composer-2.5 | 2026-07-25 | conditional log macros | a3f82c
// agent: composer-2.5 | 2026-07-25 | smoke test stderr logging | 1e5dc6
#ifndef NG_LOG_H
#define NG_LOG_H

#include <stdio.h>

#ifdef NG_SERVER
#define NG_LOG_INFO(fmt, ...)  fprintf(stderr, "NG: " fmt "\n", ##__VA_ARGS__)
#define NG_LOG_WARN(fmt, ...)  fprintf(stderr, "NG WARN: " fmt "\n", ##__VA_ARGS__)
#define NG_LOG_ERROR(fmt, ...) fprintf(stderr, "NG ERROR: " fmt "\n", ##__VA_ARGS__)
#elif defined(NG_NET_SMOKE)
#define NG_LOG_INFO(fmt, ...)  fprintf(stderr, "NG: " fmt "\n", ##__VA_ARGS__)
#define NG_LOG_WARN(fmt, ...)  fprintf(stderr, "NG WARN: " fmt "\n", ##__VA_ARGS__)
#define NG_LOG_ERROR(fmt, ...) fprintf(stderr, "NG ERROR: " fmt "\n", ##__VA_ARGS__)
#else
#include <raylib.h>
#define NG_LOG_INFO(fmt, ...)  TraceLog(LOG_INFO, "NG: " fmt, ##__VA_ARGS__)
#define NG_LOG_WARN(fmt, ...)  TraceLog(LOG_WARNING, "NG: " fmt, ##__VA_ARGS__)
#define NG_LOG_ERROR(fmt, ...) TraceLog(LOG_ERROR, "NG: " fmt, ##__VA_ARGS__)
#endif

#endif
