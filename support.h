#pragma once
#include <signal.h>
#include <stdlib.h>
extern sig_atomic_t g_require_stop;
#define REQUIRE_STOP() (g_require_stop)
#define LOG_D printf
#define LOG_W printf
