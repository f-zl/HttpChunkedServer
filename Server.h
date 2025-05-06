#pragma once
#include "ForwardList.h"
#include "support.h" // TickType_t in application data
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MAX_CLIENT_NUM (2)
#define SEND_BUF_LEN (256)
typedef enum {
  kReceivingHead, // 这里把从request line开始，到\r\n\r\n结束的部分统称为head
  kReceivingBody,
  kSending,
  kWaitSending // wait to send next chunked response
} HttpConnState;
typedef struct {
  HttpConnState state;
  size_t prevbuflen; // used by picohttpparser
  int32_t content_len;
} HttpState;
struct ElServer;
typedef struct ElConnection {
  // 先把buffer、应用层数据都放这里
  // 后面根据多协议、用户BYOB的需求重构吧
  int fd;

  unsigned char sendBuf[SEND_BUF_LEN];
  size_t toSend;  // sendBuf里还有多少要发
  size_t sendIdx; // 已经发了多少

  unsigned char *recvBuf;
  size_t toRecv;          // 还有多少要收
  size_t recvIdx;         // 已经收到多少
  size_t recvBufCapacity; // recvBuf能存储多少数据

  bool closed;      // 是否已关闭，待删除
                    // (关闭后不管另一方是否还要处理，本方已不能处理)
  size_t totalRecv; // 目前仅用于统计
  size_t totalSend;

  HttpState http;
  struct ElServer *server;
} ElConnection;

DEFINE_LIST_NODE_TYPE(ListNodeConnection, ElConnection)
DEFINE_LIST_TYPE(ListConnection, ListNodeConnection, MAX_CLIENT_NUM)

typedef struct ElServer {
  ListConnection connections;
  int toAccept; // 本循环里要加入链表的新连接，-1表示没有。用于延迟加入，使得循环里List和pollfd保持一致
  TickType_t lastSendTick;
} ElServer;

void EL_Close(ElConnection *c);
void EL_SetupToRecv(ElConnection *c, size_t toRecv, size_t recvd);
// 返回大小是否足够，如果大小不够，则所有数据都不会拷贝
bool EL_AddToSendBuffer(ElConnection *c, const void *data, size_t len);
