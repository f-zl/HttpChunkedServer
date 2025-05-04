#include "Server.h"
#include "ForwardList.h"
#include "ServerApp.h"
#include "sock.h"
#include "support.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

void EL_Close(ElConnection *c) {
  close(c->fd);
  c->closed = true;
}
void EL_SetupToRecv(ElConnection *c, size_t toRecv, size_t recvd) {
  assert(recvd < toRecv);
  c->toRecv = toRecv - recvd;
  c->recvIdx = recvd;
}

static void PrintAccept(const struct sockaddr_storage *addr, socklen_t addrLen,
                        int fd) {
  if (addrLen == sizeof(struct sockaddr_in)) {
    const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
    uint32_t addr = ntohl(a->sin_addr.s_addr);
    uint16_t port = ntohs(a->sin_port);
    LOG_D("accept %d.%d.%d.%d:%u %d\n", (int)(addr >> 24 & 0xff),
          (int)(addr >> 16 & 0xff), (int)(addr >> 8 & 0xff), (int)(addr & 0xff),
          port, fd);
  } else {
    LOG_W("Not IPv4 addr\n");
  }
}
void PrintByteArray(const unsigned char b[], size_t len, const char *prefix,
                    int fd) {
  LOG_D("%d %s [", fd, prefix);
  for (size_t i = 0; i < len; ++i) {
    LOG_D(" %02x", b[i]);
  }
  LOG_D(" ]\n");
}
#define LISTEN_FD_NUM (1) // 多少个fd用于listen
#define NFDS(clientNum) (clientNum + LISTEN_FD_NUM)

static int OnReadyAccept(ElServer *server, int fd) {
  struct sockaddr_storage addr;
  socklen_t addrLen = sizeof(addr);
  // 必须先accept，通过close来拒绝连接
  int r = accept(fd, (struct sockaddr *)&addr, &addrLen);
  if (unlikely(r == (-1))) {
    perror("accept");
    return -1;
  }
  PrintAccept(&addr, addrLen, r);
  bool doAccept = AppOnAccepting(server, &addr, addrLen);
  if (!doAccept) {
    close(r);
    LOG_D("rejected by app\n");
    return 0;
  }
  SetNonBlocking(r);
  server->toAccept = r;
  // 到循环结束再检查链表是否还有空间
  return 0;
}
static int CheckAccept(ElServer *server, struct pollfd *fds) {
  if (unlikely(fds->revents & POLLERR)) {
    // will this happen?
    LOG_W("server fd err\n");
    return -1;
  } else if (fds->revents & POLLIN) {
    if (OnReadyAccept(server, fds->fd) < 0) {
      return -1;
    }
  }
  return 0;
}
// 调用者需要知道
// 是否已经发成功，是否是后面再发，是否是其他错误
// 调用者可以用toSend来检查
static void DoSend(ElConnection *c) {
  assert(c->sendIdx + c->toSend <= sizeof(c->sendBuf));
  assert(c->toSend > 0);
  ssize_t r = send(c->fd, &c->sendBuf[c->sendIdx], c->toSend, 0);
  if (r < 0) {
    if (IsWouldBlock()) { // ignore would block
    } else {
      perror("send");
      AppOnError(c);
    }
  } else {
    c->toSend -= (size_t)r;
    c->totalSend += (size_t)r;
    // 部分发送时，数据是否应该前移，从而给后面的数据腾个位置
    // 或者设计为环形缓冲，允许后面写满后写到开头处
    if (c->toSend == 0) {
      AppOnSend(c);
      c->sendIdx = 0;
    } else {
      c->sendIdx += (size_t)r;
    }
  }
}
void EL_AddToSendBuffer(ElConnection *c, const void *data, size_t len) {
  size_t sendBufUsed = (c->sendIdx + c->toSend);
  if (sendBufUsed + len < SEND_BUF_LEN) {
    memcpy(&c->sendBuf[c->sendIdx + c->toSend], data, len);
    c->toSend += len;
  } else {
    LOG_W("send buffer full\n");
  }
}

static void DoRecv(ElConnection *c) {
  assert(c->recvIdx + c->toRecv <= sizeof(c->recvBuf));
  assert(c->toRecv > 0);
  ssize_t r = recv(c->fd, &c->recvBuf[c->recvIdx], c->toRecv, 0);
  if (unlikely(r < 0)) {
    if (IsWouldBlock()) {
      LOG_D("wouldblock\n");
    } else {
      perror("recv");
      AppOnError(c);
    }
  } else if (r > 0) {
    c->toRecv -= (size_t)r;
    c->recvIdx += (size_t)r;
    c->totalRecv += (size_t)r;
    AppOnRecv(c);
  } else { // peer closed
    AppOnPeerClose(c);
  }
}
static void CheckConnections(ElServer *server, struct pollfd fds[]) {
  size_t i = 0;
  FlNodeBase *it;
  for (it = FL_Begin(&server->connections.base);
       it != FL_End(&server->connections.base); it = it->next) {
    ElConnection *c = &((ListNodeConnection *)it)->value;
    assert(c->fd == fds[i].fd);
    const short revents = fds[i].revents;
    if (revents & POLLERR) {
      LOG_W("%d err\n", fds[i].fd);
      // 需要++i，因此不continue
      EL_Close(c);
    }
    if (revents & POLLIN) {
      DoRecv(c);
    }
    if (revents & POLLOUT) {
      DoSend(c);
    }
    if (revents & POLLHUP) { // peer closed
      // 测试下来在Linux上对方主动关闭TCP连接，不会报POLLHUP
      // 应该应用层处理？这里先相应关闭吧
      LOG_D("close %d\n", c->fd);
      EL_Close(c);
    }
    ++i;
  }
}
// 新的连接和关闭都先保留在链表里，使得链表在循环中和pollfd保持一致
// 循环结束后，需要根据连接、关闭情况更新链表
// 成功则返回Connection * (用于后面PushBackFds)，否则返回NULL
static ElConnection *CheckToAccept(ElServer *server) {
  if (server->toAccept != -1) {
    FlNodeBase *node = FL_EmplaceFront(&server->connections.base);
    if (node != NULL) {
      ElConnection *c = &((ListNodeConnection *)node)->value;
      c->fd = server->toAccept;
      c->toSend = 0;
      c->sendIdx = 0;
      c->toRecv = 0;
      c->recvIdx = 0;
      c->closed = false;
      c->totalRecv = 0;
      c->totalSend = 0;
      c->server = server;
      AppOnAccepted(c);
      return c;
    } else {
      LOG_W("List full, cannot accept\n");
      close(server->toAccept);
    }
  }
  return NULL;
}
static void PushBackFds(ElConnection *c, struct pollfd *pfd) {
  pfd->fd = c->fd;
  pfd->events = POLLHUP;
  if (c->toRecv > 0) {
    pfd->events |= POLLIN;
    // LOG_D(" IN");
  }
  if (c->toSend > 0) {
    pfd->events |= POLLOUT;
    // LOG_D(" OUT");
  }
}
static nfds_t UpdateConnectionListAndConstructPollFds(ElServer *server,
                                                      struct pollfd fds[]) {
  // 延迟处理链表的删除、新增，从而在循环中使链表和fds保持一致

  // 另外，因为都要遍历链表，所以和ConstructPollFds合并
  // 为了让poll的输入和Connection一致，并且支持关闭连接，所以每次从Connection重新构造poll的输入
  // 遍历连接的链表，根据收发请求标记POLLIN, POLLOUT

  nfds_t i = 0;
  for (FlNodeBase *it = FL_BeforeBegin(&server->connections.base);
       it->next != FL_End(&server->connections.base);) {
    ElConnection *c = &((ListNodeConnection *)(it->next))->value;
    if (c->closed) {
      FL_EraseAfter(&server->connections.base, it);
    } else {
      PushBackFds(c, &fds[i]);
      ++i;
      it = it->next;
    }
  }

  ElConnection *c = CheckToAccept(server);
  if (c != NULL) {
    PushBackFds(c, &fds[i]);
    ++i;
  }
  return i;
}
void PrintPollResult(const struct pollfd fds[], nfds_t nfds) {
  LOG_D("poll");
  for (nfds_t i = 0; i < nfds; ++i) {
    LOG_D(" %d", fds[i].fd);
    if (fds[i].revents & POLLERR) {
      LOG_D(" ERR");
    }
    if (fds[i].revents & POLLHUP) {
      LOG_D(" HUP");
    }
    if (fds[i].revents & POLLIN) {
      LOG_D(" IN");
    }
    if (fds[i].revents & POLLOUT) {
      LOG_D(" OUT");
    }
  }
  LOG_D("\n");
}
static void AppOnPollErr() { exit(1); }
static void PollLoop(ElServer *server, const int serverFd) {
  struct pollfd fds[NFDS(MAX_CLIENT_NUM)];
  fds[0].fd = serverFd;
  fds[0].events = POLLIN;
  nfds_t nfds = LISTEN_FD_NUM;
  while (!REQUIRE_EXIT()) {
    int timeout = AppCalcTimeout(server);
    assert(nfds <= ARRAY_LEN(fds));
    int n = poll(fds, nfds, timeout);
    // PrintPollResult(&fds[LISTEN_FD_NUM], nfds - LISTEN_FD_NUM);
    server->toAccept = -1; // 循环开始时设-1，如果本循环里有新的连接，则覆盖
    if (unlikely(n < 0)) {
      perror("poll");
      AppOnPollErr();
    } else if (n == 0) { // timeout
      AppOnPollTimeout(server);
    } else { // event occurs
      if (CheckAccept(server, &fds[0]) < 0) {
        // 应该不需要退出
      }
      CheckConnections(server, &fds[LISTEN_FD_NUM]);
    }
    nfds = NFDS(
        UpdateConnectionListAndConstructPollFds(server, &fds[LISTEN_FD_NUM]));
  }
}
int main(void) {
  uint16_t port = 8000;
  int serverFd = socket(AF_INET, SOCK_STREAM, 0);
  if (serverFd == -1) {
    perror("socket");
    abort();
  }
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  int r = bind(serverFd, (const struct sockaddr *)&addr, sizeof(addr));
  if (r == -1) {
    perror("bind");
    abort();
  }
  r = listen(serverFd, MAX_CLIENT_NUM);
  if (r == -1) {
    perror("listen");
    abort();
  }
  SetNonBlocking(serverFd); // man accept NOTES
  SetupSignal();
  ElServer server;
  FL_Init(&server.connections.base, sizeof(ElConnection), MAX_CLIENT_NUM);
  PollLoop(&server, serverFd);
  close(serverFd);
  for (FlNodeBase *it = FL_Begin(&server.connections.base);
       it != FL_End(&server.connections.base); it = it->next) {
    ElConnection *c = &((ListNodeConnection *)it)->value;
    close(c->fd);
  }
  LOG_D("exit\n");
}
