#include "support.h"
// Windows有GetTickCount返回32位的自系统启动以来经过的毫秒数
TickType_t xTaskGetTickCount(void) { return time(NULL); }
TickType_t GetElapsedMs(TickType_t startTick) {
  double diff = difftime(time(nullptr), startTick);
  return (TickType_t)(diff * 1000.0);
}
