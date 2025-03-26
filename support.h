#pragma once
#include <signal.h>
#include <stdlib.h>
extern sig_atomic_t g_require_stop;
#define REQUIRE_STOP() (g_require_stop)
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
