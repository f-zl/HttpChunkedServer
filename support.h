#pragma once
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

extern sig_atomic_t g_requireExit;
#define REQUIRE_EXIT() (g_requireExit)
void SetupSignal(void);

// 一般Warning是用户操作错误，Error是程序错误
#define LOG_D printf
#define LOG_W(...)                                                             \
  do {                                                                         \
    printf("Warning ");                                                        \
    printf(__VA_ARGS__);                                                       \
  } while (0)
#define LOG_E(...)                                                             \
  do {                                                                         \
    printf("Error ");                                                          \
    printf(__VA_ARGS__);                                                       \
  } while (0)

typedef time_t TickType_t;
TickType_t xTaskGetTickCount(void);
TickType_t GetElapsedMs(TickType_t startTick);

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

typedef struct {
  const char *buf;
  size_t len;
} SpanConstChar;

bool is_get(SpanConstChar method);
bool is_post(SpanConstChar method);

#ifdef __cplusplus
}
#endif
