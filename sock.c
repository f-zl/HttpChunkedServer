#include "sock.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#define REPORT_ERR_THEN_EXIT(msg)                                              \
  do {                                                                         \
    perror(msg);                                                               \
    return -1;                                                                 \
  } while (0)

int setup_tcp_server(uint16_t port, int backlog) {
  const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    REPORT_ERR_THEN_EXIT("socket");
  }
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    REPORT_ERR_THEN_EXIT("bind");
  }
  if (listen(server_fd, backlog) < 0) {
    REPORT_ERR_THEN_EXIT("listen");
  }
  return server_fd;
}
ssize_t send_all(int fd, const void *data, size_t data_len, int timeout_ms) {
  (void)timeout_ms;
  assert(data_len <= SSIZE_MAX && data_len > 0);
  size_t total = 0;
  const char *p = data;
  while (total < data_len) {
    ssize_t len = send(fd, &p[total], data_len - total, 0);
    if (len <= 0) {
      return len;
    }
    total += (size_t)len;
  }
  assert(total == data_len);
  return (ssize_t)total;
}
void SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0) {
    perror("fcntl");
    abort();
  }
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) < 0) {
    perror("fcntl");
    abort();
  }
}
bool IsWouldBlock(void) { return errno == EWOULDBLOCK || errno == EAGAIN; }
