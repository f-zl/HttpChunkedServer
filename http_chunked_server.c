#include "picohttpparser.h"
#include "sock.h"
#include "support.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>

#define MAX_CLIENT_NUM 2
#define BUF_SIZE 1024
#define NFDS(client_num) ((client_num) + 1)
typedef enum { kReceiving, kWaitSending } ConnectionState;
#define MAX_HDR_NUM 50
typedef struct {
  const char *buf;
  size_t len;
} SpanConstChar;

typedef struct {
  int fd;
  ConnectionState state; // is chunked msg being sent on this connection
  unsigned char buf[BUF_SIZE];
  size_t buf_idx;    // how many bytes have been received
  size_t prevbuflen; // used by picohttpparser
} Connection;
typedef struct {
  int client_num;
  Connection conn[MAX_CLIENT_NUM];
} Server;
static void reset_conn_data(Connection *c) {
  c->state = kReceiving;
  c->buf_idx = 0;
  c->prevbuflen = 0;
}
static void print_poll_state(const struct pollfd fds[], int client_num) {
  LOG_D("pollfds [");
  for (int i = 0; i < NFDS(client_num); ++i) {
    LOG_D(" (%x,%x,%x)", fds[i].fd, fds[i].events, fds[i].revents);
  }
  LOG_D(" ]\n");
}

static void add_client(Connection conn[], struct pollfd fds[], int client_fd,
                       int *client_num) {
  conn[*client_num].fd = client_fd;
  reset_conn_data(&conn[*client_num]);
  *client_num += 1; // increase since fds[0] is server fd
  fds[*client_num].fd = client_fd;
  fds[*client_num].events = POLLIN;
  fds[*client_num].revents = 0;
  // revents will be read in next iteration, before poll touches it
  // so it's necessary to set revents to 0

  LOG_D("%x accepted\n", client_fd);
  print_poll_state(fds, *client_num);
}
static void remove_client(Server *server, struct pollfd fds[], const int i) {
  // fds[i] is invalid, fds length = client_num + 1
  // move fds[i + 1 : client_num + 1] to fds[i : client_num]
  LOG_D("remove(%x), revents=%x\n", fds[i].fd, fds[i].revents);

  memmove(&fds[i], &fds[i + 1],
          sizeof(struct pollfd) * (size_t)(server->client_num - i));
  // FIXME 这里应该要去掉已经无效的server->conn[i-1]
  // 但buffer是直接写在server->conn里的memmove好像也不合理
  server->client_num -= 1;
  print_poll_state(fds, server->client_num);
  // 目前server->conn[i-1]在accept时复位
}
static void checked_close(int fd) {
  int r = close(fd);
  assert(r == 0);
}
static int on_ready_accept(Server *server, const int server_fd,
                           struct pollfd fds[]) {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  int r = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
  if (r < 0) {
    PERROR("accept");
    return r;
  }
  if (server->client_num >= MAX_CLIENT_NUM) {
    // reject connection by accept + close
    LOG_D("too many clients, reject\n");
    checked_close(r);
  } else {
    // do not set non blocking because
    // currently send need be blocking
    add_client(server->conn, fds, r, &server->client_num);
  }
  return 0;
}
static int calc_timeout() {
  // 返回poll的timeout值，单位ms
  // if (has_listening_client) { // 存在监听周期数据的client
  // timeout = next_cycle(period, last_send_time)
  // } else {
  // timeout = -1
  // }
  return 1000;
}
static bool is_get(SpanConstChar method) {
  if (method.len != 3) {
    return false;
  }
  return memcmp(method.buf, "GET", 3) == 0;
}
static bool is_post(SpanConstChar method) {
  if (method.len != 4) {
    return false;
  }
  return memcmp(method.buf, "POST", 4) == 0;
}
#define RC_ERR (-1)
#define RC_INCOMPLETE (-2)
static bool is_version(SpanConstChar path) {
  const char *p = "/version";
  size_t l = strlen(p);
  if (path.len != l) {
    return false;
  }
  return memcmp(path.buf, p, l) == 0;
}
static bool is_periodic(SpanConstChar path) {
  const char *p = "/periodic";
  size_t l = strlen(p);
  if (path.len != l) {
    return false;
  }
  return memcmp(path.buf, p, l) == 0;
}
/*
GET
/version               version
/sig/<addr>            signal
/param/<addr>/<length> param
/periodic              periodic msg
POST
*/
#define OK_LINE "HTTP/1.1 200 OK\r\n"
#define ALLOW_CORS_HEADER "Access-Control-Allow-Origin: *\r\n"
#define TRANSFER_ENCODING_CHUNKED_HEADER "Transfer-Encoding: chunked\r\n"
#define CONTENT_LENGTH_HEADER "Content-Length: "

static const char OK_CHUNKED_RESPONSE[79] =
    OK_LINE ALLOW_CORS_HEADER TRANSFER_ENCODING_CHUNKED_HEADER "\r\n";

// 到Content-Length: 处：长度65
#define CONTENT_LENGTH_LEN 65
static const char OK_CONTENT_LENGTH_RESPONSE[70] =
    OK_LINE ALLOW_CORS_HEADER CONTENT_LENGTH_HEADER "0\r\n\r\n";

#define MAX_CHUNK_LEN_DIGIT 3
// 最多多少个hex char可以表示长度，3个则支持最长fff长度的chunk
static const char DIGITS[16] = "0123456789abcdef";
static char hex_digit(int value) { // 0-9 -> '0'-'9', 10-15 -> 'a'-'f'
  assert(value >= 0 && value < 16);
  return DIGITS[value];
}
static char decimal_digit(int value) {
  assert(value >= 0 && value <= 9);
  return DIGITS[value];
}
static void send_chunk(int fd, const void *chunk, int len) {
  assert(len >= 0 && len <= 0xfff); // MAX_CHUNK_LEN_DIGIT设定了最多3位
  if (len == 0) {
    send(fd, "0\r\n\r\n", 5, 0);
    return;
  }
  char length_line[MAX_CHUNK_LEN_DIGIT + 2];
  // 需要根据len的实际值计算长度，写入length_line
  // len: 0-0xf则1位
  // len: 0x10-0xff则2位
  size_t line_len;
  if (len <= 0xf) {
    length_line[0] = hex_digit(len);
    length_line[1] = '\r';
    length_line[2] = '\n';
    line_len = 3;
  } else if (len <= 0xff) {
    length_line[0] = hex_digit(len >> 4);
    length_line[1] = hex_digit(len & 0xf);
    length_line[2] = '\r';
    length_line[3] = '\n';
    line_len = 4;
  } else if (len <= 0xfff) {
    length_line[0] = hex_digit(len >> 8);
    length_line[1] = hex_digit((len >> 4) & 0xf);
    length_line[2] = hex_digit(len & 0xf);
    length_line[3] = '\r';
    length_line[4] = '\n';
    line_len = 5;
  }
  send(fd, length_line, line_len, MSG_MORE);
  send(fd, chunk, (size_t)len, MSG_MORE);
  send(fd, "\r\n", 2, 0);
}
// #define DIGIT_NUM 4
// static void write_4_digits(uint16_t value, char s[DIGIT_NUM]) {
//   for (int i = 0; i < DIGIT_NUM; ++i) {
//     s[(DIGIT_NUM - 1) - i] = (char)((value % 10) + '0');
//     value /= 10;
//   }
// }
static size_t num_to_chars(int value, char s[4]) { // tested
  assert(value <= 9999 && value >= 0);
  // 0-9
  // 10-99
  // 100-999
  // 1000-9999
  if (value <= 9) {
    s[0] = decimal_digit(value);
    return 1;
  }
  int one, ten, hundred, thousand;
  if (value <= 99) {
    ten = value / 10;
    one = value % 10;
    s[0] = decimal_digit(ten);
    s[1] = decimal_digit(one);
    return 2;
  }
  if (value <= 999) {
    one = value % 10;
    value /= 10;
    ten = value % 10;
    hundred = value / 10;
    s[0] = decimal_digit(hundred);
    s[1] = decimal_digit(ten);
    s[2] = decimal_digit(one);
    return 3;
  }
  one = value % 10;
  value /= 10;
  ten = value % 10;
  value /= 10;
  hundred = value % 10;
  thousand = value / 10;
  s[0] = decimal_digit(thousand);
  s[1] = decimal_digit(hundred);
  s[2] = decimal_digit(ten);
  s[3] = decimal_digit(one);
  return 4;
}
static void send_content_length(Connection *c, size_t len) {
  assert(len <= 9999);
  char s[8];
  size_t l = num_to_chars((int)len, s);
  assert(l <= 4);
  memcpy(&s[l], "\r\n\r\n", 4);
  send(c->fd, s, l + 4, MSG_MORE);
}
static void send_ok_response(Connection *c, const void *body, size_t body_len) {
  assert(body_len <= 9999); // 最大支持4 digits
  // assert(strlen(OK_HDR) == OK_HDR_LEN);
  // send(c->fd, body, body_len, 0);
  // send_all(c->fd, OK_HDR, strlen(OK_HDR), 500);
  if (body_len > 0) {
    send(c->fd, OK_CONTENT_LENGTH_RESPONSE, CONTENT_LENGTH_LEN, MSG_MORE);
    send_content_length(c, body_len);
    send_all(c->fd, body, body_len, 500);
  } else {
    send_all(c->fd, OK_CONTENT_LENGTH_RESPONSE,
             sizeof(OK_CONTENT_LENGTH_RESPONSE), 500);
  }
}
static uint32_t g_tick = 0;
static void send_ok_chunked_response(Connection *c) {
  send(c->fd, OK_CHUNKED_RESPONSE, sizeof(OK_CHUNKED_RESPONSE), MSG_MORE);
  uint32_t t = htonl(g_tick);
  send_chunk(c->fd, &t, 4);
}
static ssize_t process_request(Connection *c, SpanConstChar method,
                               SpanConstChar path,
                               struct phr_header headers[MAX_HDR_NUM],
                               size_t num_headers) {
  (void)path;
  (void)headers;
  (void)num_headers;
  if (is_get(method)) {
    if (is_version(path)) {
      const char *p = "1.1.0 " __DATE__ " " __TIME__;
      send_ok_response(c, p, strlen(p));
    } else if (is_periodic(path)) {
      send_ok_chunked_response(c);
      c->state = kWaitSending;
    }
    // const char *reply = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello";
    // strcpy((char *)c->buf, reply);
    // return (ssize_t)strlen(reply);
    return -2;
  } else if (is_post(method)) {

  } else {
  }
  return -2;
}
// 如果>0，则有数据回复
// 如果=-1，则close
// 如果=-2，则继续接收
static ssize_t on_received(Connection *c) {
  // 收到新数据
  // 尝试解析请求并处理
  SpanConstChar method;
  SpanConstChar path;
  int minor_version;
  struct phr_header headers[MAX_HDR_NUM];
  size_t num_headers = MAX_HDR_NUM;
  // 这个还是stream parser，可以对一个stream反复调用
  int pret = phr_parse_request(
      (const char *)c->buf, c->buf_idx, &method.buf, &method.len, &path.buf,
      &path.len, &minor_version, headers, &num_headers, c->prevbuflen);
  if (pret > 0) {
    printf("request is %d bytes long\n", pret);
    printf("method is %.*s\n", (int)method.len, method.buf);
    printf("path is %.*s\n", (int)path.len, path.buf);
    printf("HTTP version is 1.%d\n", minor_version);
    printf("headers:\n");
    for (size_t i = 0; i != num_headers; ++i) {
      printf("%.*s: %.*s\n", (int)headers[i].name_len, headers[i].name,
             (int)headers[i].value_len, headers[i].value);
    }
    return process_request(c, method, path, headers, num_headers);
  } else if (pret == -1) {
    LOG_W("parse err\n");
    return -1; // should close connection
  } else {
    assert(pret == -2); // incomplete
    c->prevbuflen = c->buf_idx;
    return RC_INCOMPLETE;
  }
}
// 返回0表示正常
// 返回-1表示错误或者对方断开连接，应关闭连接
static ssize_t on_readable(Connection *c) {
  if (BUF_SIZE <= c->buf_idx) {
    LOG_W("read buffer full\n");
    return -1;
  }
  const size_t buf_remain_len = BUF_SIZE - c->buf_idx;
  ssize_t r = recv(c->fd, &c->buf[c->buf_idx], buf_remain_len, 0);
  // 调用前已判断无POLLERR和POLLHUP，因此这里recv返回值必>0
  if (r <= 0) {
    LOG_W("recv %d despite POLLIN\n", (int)r);
    return -1;
  }
  c->buf_idx += (size_t)r;
  if (c->state != kReceiving) {
    LOG_W("unexpected recv\n");
    return -1;
  }
  ssize_t rst = on_received(c);
  if (rst > 0) {
    // ssize_t r = send_all(c->fd, c->buf, (size_t)rst, 500);
    // if (r < 0) { // TODO or timeout
    //   PERROR("send");
    //   return -1;
    // }
    return 0;
  } else if (rst == -1) {
    return -1;
  } else {
    assert(rst == RC_INCOMPLETE);
    // wait for next recv, do nothing by now
    return 0;
  }
}
static void on_poll_timeout(Server *server) {
  for (int i = 0; i < server->client_num; ++i) {
    if (server->conn[i].state == kWaitSending) {
      ++g_tick;
      uint32_t t = htonl(g_tick);
      send_chunk(server->conn[i].fd, &t, 4);
      printf("send_chunk(%08x)\n", g_tick);
      if (g_tick > 5) {
        send_chunk(server->conn[i].fd, NULL, 0);
        server->conn[i].state = kReceiving;
      }
    }
  }
}
static void on_poll_event(Server *server) {
  // poll return > 0 (not timeout)
  (void)server;
}
static void poll_loop(Server *server, const int server_fd) {
  struct pollfd fds[NFDS(MAX_CLIENT_NUM)];
  fds[0].fd = server_fd;
  fds[0].events = POLLIN;
  server->client_num = 0;
  while (!REQUIRE_STOP()) {
    const int timeout_ms = calc_timeout();
    const int n = poll(fds, (nfds_t)NFDS(server->client_num), timeout_ms);
    if (n < 0) { // unlikely
      PERROR("poll");
      break;
    } else if (n == 0) {
      on_poll_timeout(server);
    } else {
      // these events should never happen to server fd
      assert((fds[0].revents & (POLLNVAL | POLLHUP)) == 0);
      if (fds[0].revents & POLLERR) {
        // will this happen?
        LOG_D("server fd err\n");
        break;
      } else if (fds[0].revents & POLLIN) {
        if (on_ready_accept(server, server_fd, fds) < 0) {
          break;
        }
      }
      // iterate over clients
      for (int i = 1; i < NFDS(server->client_num); ++i) {
        assert((fds[i].revents & POLLNVAL) == 0);
        if (fds[i].revents & (POLLERR | POLLHUP)) {
          // 对方关闭则关闭
          checked_close(fds[i].fd);
          remove_client(server, fds, i);
          --i; // update loop index due to array length change
        } else if (fds[i].revents & POLLIN) {
          // fds[i] is in conn[i-1]
          if (on_readable(&server->conn[i - 1]) < 0) {
            checked_close(fds[i].fd);
            remove_client(server, fds, i);
            --i;
          }
        }
      }
      on_poll_event(server);
    }
  }
  // some fd may be invalid, so close may fail
  for (int i = 0; i < NFDS(server->client_num); ++i) {
    close(fds[i].fd);
  }
}
sig_atomic_t g_require_stop = 0;
static void sig_handler(int signo, siginfo_t *info, void *context) {
  (void)info;
  (void)context;
  if (signo == SIGINT) {
    g_require_stop = 1;
  }
}
static void setup_signal(void) {
  struct sigaction act = {0};
  act.sa_flags = SA_SIGINFO;
  act.sa_sigaction = &sig_handler;
  if (sigaction(SIGINT, &act, NULL) == -1) {
    // signal() doesn't interrupt accept, but sigaction does
    // why? To be tested on a native Linux machine
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}
static Server s_server;
int main(void) {
  setup_signal();
  uint16_t port = 8000;
  int backlog = 1;
  int fd = setup_tcp_server(port, backlog);
  assert(fd >= 0);
  poll_loop(&s_server, fd);
  LOG_D("exit\n");
}
