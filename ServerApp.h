#pragma once
#include <arpa/inet.h>
#include <stdbool.h>
typedef struct ElServer ElServer;
typedef struct ElConnection ElConnection;
// recv返回>0时调用
void AppOnRecv(ElConnection *c);
// send返回>0时调用
void AppOnSend(ElConnection *c);

void AppOnPeerClose(ElConnection *c);
void AppOnError(ElConnection *c);
// 返回应用层是否觉得应该接收
bool AppOnAccepting(ElServer *server, const struct sockaddr_storage *addr,
                    socklen_t addrLen);
void AppOnAccepted(ElConnection *c);
void AppOnPollTimeout(ElServer *server);
// 返回poll的timeout值，单位ms，-1为一直等待
int AppCalcTimeout(ElServer *server);
void AppOnClosing(ElConnection *c);
