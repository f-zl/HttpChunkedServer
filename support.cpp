#include "support.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
sig_atomic_t g_requireExit;
static void SigHandler(int signo, siginfo_t *info, void *context) {
  (void)info;
  (void)context;
  if (signo == SIGINT) {
    g_requireExit = 1;
  }
}
void SetupSignal(void) {
  struct sigaction act = {};
  act.sa_flags = SA_SIGINFO;
  act.sa_sigaction = &SigHandler;
  if (sigaction(SIGINT, &act, NULL) == -1) {
    perror("sigaction");
    abort();
  }
}
// Windows有GetTickCount返回32位的自系统启动以来经过的毫秒数
TickType_t xTaskGetTickCount(void) { return time(NULL); }
TickType_t GetElapsedMs(TickType_t startTick) {
  double diff = difftime(time(nullptr), startTick);
  return (TickType_t)(diff * 1000.0);
}
bool is_get(SpanConstChar method) {
  if (method.len != 3) {
    return false;
  }
  return memcmp(method.buf, "GET", 3) == 0;
}
bool is_post(SpanConstChar method) {
  if (method.len != 4) {
    return false;
  }
  return memcmp(method.buf, "POST", 4) == 0;
}
