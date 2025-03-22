#pragma once
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h> // perror
#include <sys/socket.h>
#include <unistd.h> // close
#define PERROR(msg) perror(msg)

int setup_tcp_server(uint16_t port, int backlog);
ssize_t send_all(int fd, const void *data, size_t data_len, int timeout_ms);
