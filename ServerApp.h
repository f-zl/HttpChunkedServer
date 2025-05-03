#pragma once
#include <arpa/inet.h>
#include <stdbool.h>
typedef struct Server Server;
typedef struct Connection Connection;
// recv返回>0时调用
void AppOnRecv(Connection *c);
// send返回>0时调用
void AppOnSend(Connection *c);

void AppOnPeerClose(Connection *c);
void AppOnError(Connection *c);
// 返回应用层是否觉得应该接收
bool AppOnAccepting(Server *server, const struct sockaddr_storage *addr,
                    socklen_t addrLen);
void AppOnAccepted(Connection *c);
