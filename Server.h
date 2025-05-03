#pragma once
#include "List.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MAX_CLIENT_NUM (2)
#define SEND_BUF_LEN (256)
#define RECV_BUF_LEN (1024)
typedef enum {
  kReceivingHead, // 这里把从request line开始，到\r\n\r\n结束的部分统称为head
  kReceivingBody,
  kSending,
  kWaitSending // wait to send next chunked response
} ConnectionState;
typedef struct {
  ConnectionState state;
  size_t prevbuflen; // used by picohttpparser
  int32_t content_len;
} HttpState;
typedef struct Connection {
  // 先把buffer、应用层数据都放这里
  // 后面根据多协议、用户BYOB的需求重构吧
  int fd;
  unsigned char sendBuf[SEND_BUF_LEN];
  size_t toSend;  // sendBuf里还有多少要发
  size_t sendIdx; // 已经发了多少
  unsigned char recvBuf[RECV_BUF_LEN];
  size_t toRecv;    // 还有多少要收
  size_t recvIdx;   // 已经收到多少
  bool closed;      // 是否已关闭，待删除
                    // (关闭后不管另一方是否还要处理，本方已不能处理)
  size_t totalRecv; // 目前仅用于统计
  size_t totalSend;

  HttpState http;
} Connection;

DEFINE_LIST_NODE_TYPE(ListNodeConnection, Connection)
DEFINE_LIST_TYPE(ListConnection, ListNodeConnection, MAX_CLIENT_NUM)

typedef struct Server {
  ListConnection connections;
  int toAccept; // 本循环里要加入链表的新连接，-1表示没有。用于延迟加入，使得循环里List和pollfd保持一致
} Server;

void MyClose(Connection *c);
void SetupToRecv(Connection *c, size_t toRecv, size_t recvd);
void AddToSendBuffer(Connection *c, const void *data, size_t len);
