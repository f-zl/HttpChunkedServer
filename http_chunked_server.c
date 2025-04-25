#include "Buffer.h"
#include "impl.h"
#include "picohttpparser.h"
#include "sock.h"
#include "support.h"
#include <assert.h>
#include <ctype.h>
#include <netinet/tcp.h> // TCP_NODELAY
#include <stdbool.h>
#include <string.h>

#define MAX_CLIENT_NUM 2
#define BUF_SIZE 1024
#define NFDS(client_num) ((client_num) + 1)
typedef enum {
  kReceivingHead, // 这里把从request line开始，到\r\n\r\n结束的部分统称为head
  kReceivingBody,
  kSending,
  kWaitSending // wait to send next chunked response
} ConnectionState;
#define MAX_HDR_NUM 20
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
  int32_t content_len;
} Connection;
typedef struct {
  int client_num;
  Connection conn[MAX_CLIENT_NUM];
  TickType_t last_send_tick;
} Server;
static void reset_conn_data(Connection *c) {
  c->state = kReceivingHead;
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
const uint16_t PERIOD_MS = 1000;
// 是否存在监听周期数据的client
static bool has_listening_client(Server *server) {
  for (int i = 0; i < server->client_num; ++i) {
    if (server->conn[i].state == kWaitSending) {
      return true;
    }
  }
  return false;
}
// 距下次发送还有多少ms
static int to_next(uint16_t period, TickType_t last_send_tick) {
  TickType_t elapsed = GetElapsedMs(last_send_tick);
  if (elapsed >= (TickType_t)period) {
    return 0;
  }
  return (int)((TickType_t)period - elapsed);
}
// 返回poll的timeout值，单位ms，-1为一直等待
static int calc_timeout(Server *server) {
  int timeout;
  if (has_listening_client(server)) {
    timeout = to_next(PERIOD_MS, server->last_send_tick);
  } else {
    timeout = -1;
  }
  return timeout;
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
#define RC_OK (0)
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

static bool is_get_param(SpanConstChar path, uint16_t *addr, uint16_t *len) {
  // /param/00000000
  const char *p = "/param/";
  const size_t l = strlen(p);
  if (path.len != 15) {
    return false;
  }
  if (memcmp(path.buf, p, l) != 0) {
    return false;
  }
  OptionU16 opt_addr = lower_hex_to_u16(&path.buf[7]);
  if (opt_addr.has_value == 0) {
    return false;
  }
  OptionU16 opt_len = lower_hex_to_u16(&path.buf[11]);
  if (opt_len.has_value == 0) {
    return false;
  }
  *addr = opt_addr.value;
  *len = opt_len.value;
  return true;
}
static bool is_param(SpanConstChar path) {
  const char *p = "/param";
  size_t l = strlen(p);
  if (path.len != l) {
    return false;
  }
  return memcmp(path.buf, p, l) == 0;
}
static bool is_image(SpanConstChar path) {
  const char *p = "/image";
  size_t l = strlen(p);
  if (path.len != l) {
    return false;
  }
  return memcmp(path.buf, p, l) == 0;
}
/* clang-format off
GET
/version               version
/sig/<addr>            signal
/param/<addr><length>  param, addr和length都是u16，由4个小写16进制字符组成
/periodic              periodic msg
POST
/param                 body的前2字节是addr，后2字节是length，后面length字节是数据，LE
/image                 body内容为镜像文件，最后2字节是crc LE
clang-format on
*/
#define HTTP_VERSION "HTTP/1.1 "
#define OK_LINE HTTP_VERSION "200 OK\r\n"
#define BAD_REQUEST_LINE HTTP_VERSION "400 Bad Request\r\n"
#define NOT_FOUND_LINE HTTP_VERSION "404 Not Found\r\n"
#define ALLOW_CORS_HEADER "Access-Control-Allow-Origin: *\r\n"
#define TRANSFER_ENCODING_CHUNKED_HEADER "Transfer-Encoding: chunked\r\n"
#define CONTENT_LENGTH_HEADER "Content-Length: "

static const char OK_CHUNKED_RESPONSE[79] =
    OK_LINE ALLOW_CORS_HEADER TRANSFER_ENCODING_CHUNKED_HEADER "\r\n";

// 到Content-Length: 处，不含数值：长度65
#define OK_CONTENT_LENGTH_LEN 65
static const char OK_CONTENT_LENGTH_RESPONSE[70] =
    OK_LINE ALLOW_CORS_HEADER CONTENT_LENGTH_HEADER "0\r\n\r\n";

#define BAD_REQUEST_CONTENT_LENGTH_LEN 74
static const char BAD_REQUEST_CONTENT_LENGTH_RESPONSE[79] =
    BAD_REQUEST_LINE ALLOW_CORS_HEADER CONTENT_LENGTH_HEADER "0\r\n\r\n";

static const char NOT_FOUND_RESPONSE[77] =
    NOT_FOUND_LINE ALLOW_CORS_HEADER CONTENT_LENGTH_HEADER "0\r\n\r\n";

#define MAX_CHUNK_LEN_DIGIT 3
// 最多多少个hex char可以表示长度，3个则支持最长fff长度的chunk

// 表示结束
// static void send_empty_chunk(int fd) { send(fd, "0\r\n\r\n", 5, 0); }
static void send_chunk_with_data(int fd, const void *chunk, int len) {
  assert(len > 0 && len <= 0xfff); // MAX_CHUNK_LEN_DIGIT设定了最多3位
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
static void send_content_length(Connection *c, size_t len) {
  assert(len <= 9999);
  char s[8];
  size_t l = num_to_four_chars((int)len, s);
  assert(l <= 4);
  memcpy(&s[l], "\r\n\r\n", 4);
  send(c->fd, s, l + 4, MSG_MORE);
}
// HTTP 200
static void send_ok_response(Connection *c, const void *body,
                             size_t body_len) { // 200
  assert(body_len <= 9999);                     // 最大支持4 digits
  if (body_len > 0) {
    send(c->fd, OK_CONTENT_LENGTH_RESPONSE, OK_CONTENT_LENGTH_LEN, MSG_MORE);
    send_content_length(c, body_len);
    send_all(c->fd, body, body_len, 500);
  } else {
    send_all(c->fd, OK_CONTENT_LENGTH_RESPONSE,
             sizeof(OK_CONTENT_LENGTH_RESPONSE), 500);
  }
}
// HTTP 400
static void send_bad_request_response(Connection *c, const void *body,
                                      size_t body_len) {
  assert(body_len <= 9999); // 最大支持4 digits
  if (body_len > 0) {
    send(c->fd, BAD_REQUEST_CONTENT_LENGTH_RESPONSE,
         BAD_REQUEST_CONTENT_LENGTH_LEN, MSG_MORE);
    send_content_length(c, body_len);
    send_all(c->fd, body, body_len, 500);
  } else {
    send_all(c->fd, BAD_REQUEST_CONTENT_LENGTH_RESPONSE,
             sizeof(BAD_REQUEST_CONTENT_LENGTH_RESPONSE), 500);
  }
}
static uint32_t g_tick = 0;
typedef struct {
  uint32_t param1;
  uint32_t param2;
} Param;
static Param g_param = {0x12345678, 0x90abcdef};
static Server s_server;
static void send_ok_chunked_response(Connection *c) {
  send(c->fd, OK_CHUNKED_RESPONSE, sizeof(OK_CHUNKED_RESPONSE), MSG_MORE);
  uint32_t t = htonl(g_tick);
  send_chunk_with_data(c->fd, &t, 4);
  s_server.last_send_tick = xTaskGetTickCount();
}
static void on_get_param(Connection *c, uint16_t addr, uint16_t len) {
  if ((len > 0) && (addr + len <= sizeof(Param))) {
    char *p = (char *)&g_param;
    send_ok_response(c, &p[addr], len);
  } else {
    send_bad_request_response(c, NULL, 0);
  }
}
static void on_post_param2(Connection *c, int request_len,
                           int32_t content_len) {
  // TODO 假设Content-Length已经收满
  // 检查数据是否收完 (需要能告知上层数据还没收完，继续收)
  assert(request_len > 0 && content_len >= 0);
  size_t total_recv = (size_t)request_len + (size_t)content_len;
  if (c->buf_idx < total_recv) {
    LOG_E("Body hasn't been fully received\n");
    return;
  }
  const uint16_t header_len = 2;   // HTTP body里，取2字节用作地址信息
  if (content_len <= header_len) { // 只有header没有数值也是错误
    const char *p = "Wrong format";
    send_bad_request_response(c, p, strlen(p));
    return;
  }
  const unsigned char *body = &c->buf[request_len];
  const uint16_t addr = ReadUint16LE(body);
  const int32_t value_len = content_len - header_len;
  LOG_D("POST /param(%d,%d)\n", addr, value_len);
  if ((size_t)addr + (size_t)value_len <= sizeof(Param)) {
    char *p = (char *)&g_param;
    memcpy(&p[addr], &body[header_len], (size_t)value_len);
    send_ok_response(c, NULL, 0);
  } else {
    const char *p = "Wrong value";
    send_bad_request_response(c, p, strlen(p));
  }
}
static void on_post_param(Connection *c, const unsigned char *body,
                          int32_t content_len) {
  assert(content_len >= 0);
  const uint16_t header_len = 2;   // HTTP body里，取2字节用作地址信息
  if (content_len <= header_len) { // 只有header没有数值也是错误
    const char *p = "Wrong format";
    send_bad_request_response(c, p, strlen(p));
    return;
  }
  const uint16_t addr = ReadUint16LE(body);
  const int32_t value_len = content_len - header_len;
  LOG_D("POST /param(%d,%d)\n", addr, value_len);
  if ((size_t)addr + (size_t)value_len <= sizeof(Param)) {
    char *p = (char *)&g_param;
    memcpy(&p[addr], &body[header_len], (size_t)value_len);
    send_ok_response(c, NULL, 0);
  } else {
    const char *p = "Wrong value";
    send_bad_request_response(c, p, strlen(p));
  }
}
static void on_post_image(Connection *c, const unsigned char *body,
                          int32_t content_len) {
  (void)c;
  (void)body;
  LOG_D("POST /image %d\n", content_len);
}
static bool match_content_length(const char *s, size_t len) {
  const char *c = "content-length";
  if (len != strlen(c)) { // 是否要支持空白符？
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (tolower(s[i]) != c[i]) {
      return false;
    }
  }
  return true;
}
// -1表示有header但错误，-2表示没有这个header，其他表示content-length值(范围0~9999999)
static int32_t find_content_length(struct phr_header headers[MAX_HDR_NUM],
                                   size_t num_headers) {
  for (size_t i = 0; i < num_headers; ++i) {
    if (match_content_length(headers[i].name,
                             headers[i].name_len)) { // 只看第1个，不检查重复
      int32_t content_len =
          chars_to_num(headers[i].value, headers[i].value_len);
      return content_len == -1 ? -1 : content_len;
    }
  }
  return -2;
}
// static void on_post_image(Connection *c, int request_len, int32_t
// content_len) {} 如果>0，则有数据回复 如果=-1，则close 如果=-2，则继续接收
// 需要一个url查callback的机制
// url可能有参数，怎么对匹配做抽象？
// 可以用url检查函数做接口，并且支持检查函数将数据传给后面的函数
static ssize_t process_request(Connection *c, SpanConstChar method,
                               SpanConstChar path,
                               struct phr_header headers[MAX_HDR_NUM],
                               size_t num_headers, int request_len) {
  // TODO 应该统一检查Content-Length，看是否收完，或者EOF？
  // GET应该检查没有更多的数据，POST统一处理Content-Length
  if (is_get(method)) {
    if (is_version(path)) {
      const char *p = "1.1.0 " __DATE__ " " __TIME__;
      send_ok_response(c, p, strlen(p));
    } else if (is_periodic(path)) {
      send_ok_chunked_response(c);
      c->state = kWaitSending;
    } else {
      uint16_t addr;
      uint16_t len;
      if (is_get_param(path, &addr, &len)) { // 可能路径OK但参数误应该报另外的错
        on_get_param(c, addr, len);
      } else {
        // 404
        send_all(c->fd, NOT_FOUND_RESPONSE, sizeof(NOT_FOUND_RESPONSE), 500);
      }
    }
    return -2;
  } else if (is_post(method)) {
    // POST body可传数据，如果有参数，用body传二进制数据
    const int32_t content_len = find_content_length(headers, num_headers);
    if (content_len == -1 || content_len == -2) {
      // 没有content-length，不支持，可以直接断连？或者400？
      const char *p = "Fail to get Content-Length";
      send_bad_request_response(c, p, strlen(p));
    } else {
      assert(content_len >= 0 && content_len <= 9999999);
      if (is_param(path)) {
        on_post_param2(c, request_len, content_len);
      } else if (is_image(path)) {
        on_post_image(c, &c->buf[request_len], content_len);
        // 暂时不支持
        send_all(c->fd, NOT_FOUND_RESPONSE, sizeof(NOT_FOUND_RESPONSE), 500);
      } else {
        // 404
        send_all(c->fd, NOT_FOUND_RESPONSE, sizeof(NOT_FOUND_RESPONSE), 500);
      }
    }
  } else {
    const char *p = "Method not supported";
    send_bad_request_response(c, p, strlen(p));
    // 400? 405?
    // 非法的访问是否可以全部回复400 (body来区分错误)
  }
  return -2;
}
// 如果>0，则有数据回复
// 如果=-1，则close
// 如果=-2，则继续接收
/*static*/ ssize_t on_received(Connection *c) {
  // 收到新数据
  // 尝试解析请求并处理
  SpanConstChar method;
  SpanConstChar path;
  int minor_version;
  struct phr_header headers[MAX_HDR_NUM];
  size_t num_headers = MAX_HDR_NUM;
  // 这个是stream parser，可以对一个stream反复调用 (但基本都要从头开始parse)
  int pret = phr_parse_request(
      (const char *)c->buf, c->buf_idx, &method.buf, &method.len, &path.buf,
      &path.len, &minor_version, headers, &num_headers, c->prevbuflen);
  if (pret > 0) {
    LOG_D("request len %d bytes\n", pret); // header的长度，不含body
    LOG_D("method %.*s\n", (int)method.len, method.buf);
    LOG_D("path %.*s\n", (int)path.len, path.buf);
    LOG_D("HTTP version 1.%d\n", minor_version);
    LOG_D("headers:\n");
    for (size_t i = 0; i != num_headers; ++i) {
      LOG_D("%.*s: %.*s\n", (int)headers[i].name_len, headers[i].name,
            (int)headers[i].value_len, headers[i].value);
    }
    return process_request(c, method, path, headers, num_headers, pret);
    // 如果处理完请求，应该把缓存长度归0
  } else if (pret == -1) {
    LOG_W("parse err\n");
    return RC_ERR; // should close connection
  } else {
    assert(pret == -2); // incomplete
    c->prevbuflen = c->buf_idx;
    return RC_INCOMPLETE;
  }
}
static void process_get_request(Connection *c, SpanConstChar path) {
  if (is_version(path)) {
    const char *p = "1.1.0 " __DATE__ " " __TIME__;
    send_ok_response(c, p, strlen(p));
  } else if (is_periodic(path)) {
    send_ok_chunked_response(c);
    c->state = kWaitSending;
  } else {
    uint16_t addr;
    uint16_t len;
    if (is_get_param(path, &addr, &len)) { // 可能路径OK但参数误应该报另外的错
      on_get_param(c, addr, len);
    } else {
      // 404
      send_all(c->fd, NOT_FOUND_RESPONSE, sizeof(NOT_FOUND_RESPONSE), 500);
    }
  }
}
static void process_post_request(Connection *c, SpanConstChar path,
                                 const unsigned char *body,
                                 int32_t content_len) {
  // POST body可传数据，如果有参数，用body传二进制数据
  assert(content_len >= 0 && content_len <= 9999999);
  if (is_param(path)) {
    on_post_param(c, body, content_len);
  } else if (is_image(path)) {
    on_post_image(c, NULL, content_len);
    // 暂时不支持
    send_all(c->fd, NOT_FOUND_RESPONSE, sizeof(NOT_FOUND_RESPONSE), 500);
  } else {
    // 404
    send_all(c->fd, NOT_FOUND_RESPONSE, sizeof(NOT_FOUND_RESPONSE), 500);
  }
}
static int process_head(Connection *c, SpanConstChar method, SpanConstChar path,
                        struct phr_header headers[MAX_HDR_NUM],
                        size_t num_headers, size_t head_len) {
  (void)path;
  if (is_get(method)) {
    // process and send response
    // for simplicity, discard extra data
    // (or close connection?)
    // GET with body is not supported
    // more than one request at a time is not supported
    if (c->buf_idx > head_len) {
      LOG_W("GET with extra data\n");
    }
    process_get_request(c, path);
    return RC_OK;
  } else if (is_post(method)) {
    const int32_t content_len = find_content_length(headers, num_headers);
    if (content_len < 0) {
      LOG_W("Content-Length %d\n", content_len);
      // send error response, or close connection?
      return RC_ERR;
    } else if ((size_t)content_len <= (c->buf_idx - head_len)) {
      // can be processed
      // 索性应用层不用header
      process_post_request(c, path, &c->buf[head_len], content_len);
      return RC_OK;
    } else {
      // if is /image, copy data to image buffer, store future data there
      // 怎么存放header (不用存，用个flag知道是/image即可)
      // 是否要为body另开一个缓存？
      c->state = kReceivingBody;
      c->content_len = content_len;
      return RC_INCOMPLETE;
    }
  } else {
    // only PATCH, POST, and PUT requests have a body
    // send error response, or wait for complete request? or close?
    // for simplicity, just close
    return RC_ERR;
  }
}
static int on_recv_head(Connection *c) {
  SpanConstChar method;
  SpanConstChar path;
  int minor_version;
  struct phr_header headers[MAX_HDR_NUM];
  size_t num_headers = MAX_HDR_NUM;
  // picohttpparser可以对一个stream反复调用 (但基本都要从头开始parse)
  int pret = phr_parse_request(
      (const char *)c->buf, c->buf_idx, &method.buf, &method.len, &path.buf,
      &path.len, &minor_version, headers, &num_headers, c->prevbuflen);
  LOG_D("phr_parse=%d\n", pret);
  if (pret > 0) {
    // if err, close
    // if body incomplete, keep receving
    // if ok, recv next
    int r = process_head(c, method, path, headers, num_headers, (size_t)pret);
    switch (r) {
    case RC_OK:
      return RC_OK;
    case RC_INCOMPLETE: // head is complete while full request not
      c->state = kReceivingBody;
      return RC_INCOMPLETE;
    default: // RC_ERR
      return r;
    }
  } else if (pret == -2) { // incomplete
    c->prevbuflen = c->buf_idx;
    return RC_INCOMPLETE;
  } else {
    LOG_W("header parse err\n");
    return RC_ERR;
  }
}
static int on_recv_body(Connection *c) {
  if (c->buf_idx < (size_t)c->content_len) {
    return RC_INCOMPLETE;
  }
  // 收满body，可以处理
  // 根据on_recv_head的实现，只会是POST (header数据存到哪里？)
  return RC_OK;
}
static void clear_read_buf(Connection *c) { c->buf_idx = 0; }
// 返回0表示正常
// 返回-1表示错误或者对方断开连接，应关闭连接
static ssize_t on_readable(Connection *c) {
  /* TODO
  每个连接设计一个buffer (应用层管理吧)
  把发送数据先写入buffer，然后发送
  发不完就监听POLLOUT，能发时发
  */
  if (BUF_SIZE <= c->buf_idx) {
    LOG_W("read buffer full\n");
    return -1;
  }
  const size_t buf_remain_len = BUF_SIZE - c->buf_idx;
  ssize_t r = recv(c->fd, &c->buf[c->buf_idx], buf_remain_len, 0);
  // 调用前已判断无POLLERR和POLLHUP，有POLLIN
  if (r == 0) { // Linux上对方断连返回POLLIN而非POLLHUP
    return -1;
  } else if (r < 0) {
    LOG_E("recv %d with POLLIN\n", (int)r);
    return -1;
  }
  c->buf_idx += (size_t)r;
  int rst;
  switch (c->state) {
  case kReceivingHead:
    LOG_D("recv %zu in head state\n", c->buf_idx);
    rst = on_recv_head(c);
    switch (rst) {
    case RC_ERR:
      return -1;
    case RC_OK:
      clear_read_buf(c);
      break;
    }
    return 0;
  case kReceivingBody:
    LOG_D("recv %zu in body state\n", c->buf_idx);
    rst = on_recv_body(c);
    if (rst == RC_ERR) {
      return -1;
    }
    return 0;
  default: // 其他状态都不该收到数据
    LOG_E("recv in state %d\n", c->state);
    return -1;
  }
#if 0
  ssize_t rst = on_received(c);
  if (rst > 0) {
    c->buf_idx = 0;
    // FIXME
    // 需要正确处理没收全、收完回复了没有新数据、收完回复了没有新数据还有数据没处理之类的情况
    return 0;
  } else if (rst == -1) {
    return -1;
  } else {
    assert(rst == RC_INCOMPLETE);
    // wait for next recv, do nothing by now
    c->buf_idx = 0; // FIXME 需要正确处理
    return 0;
  }
#endif
}
static void on_poll_timeout(Server *server) {
  for (int i = 0; i < server->client_num; ++i) {
    if (server->conn[i].state == kWaitSending) {
      ++g_tick;
      uint32_t t = htonl(g_tick);
      send_chunk_with_data(server->conn[i].fd, &t, 4);
      server->last_send_tick =
          xTaskGetTickCount(); // TODO do not update if send fail
      printf("send_chunk(%08x)\n", g_tick);
      // if (g_tick > 5) {
      //   send_empty_chunk(server->conn[i].fd);
      //   server->conn[i].state = kReceiving;
      // }
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
    const int timeout_ms = calc_timeout(server);
    const int n = poll(fds, (nfds_t)NFDS(server->client_num), timeout_ms);
    if (n < 0) { // unlikely
      PERROR("poll");
      break;
    } else if (unlikely(n == 0)) {
      on_poll_timeout(server);
    } else {
      // these events should never happen to server fd
      assert((fds[0].revents & (POLLNVAL | POLLHUP)) == 0);
      if (fds[0].revents & POLLERR) {
        // will this happen?
        LOG_W("server fd err\n");
      } else if (fds[0].revents & POLLIN) {
        if (on_ready_accept(server, server_fd, fds) < 0) {
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
        } else {
          if (fds[i].revents & POLLIN) {
            // fds[i] is in conn[i-1]
            if (on_readable(&server->conn[i - 1]) < 0) {
              checked_close(fds[i].fd);
              remove_client(server, fds, i);
              --i;
            }
          }
          if (fds[i].revents & POLLOUT) {
            // 如果当前有要发的数据，应该在pollfds里设置event为发送，在POLLOUT时发送
            // 由于应用层发送时不需要接收，可以不用POLLIN？
            // 还是要检查POLLIN，如果有数据，应用层决定断连
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
int main(void) {
  setup_signal();
  uint16_t port = 8000;
  int backlog = 1;
  int fd = setup_tcp_server(port, backlog);
  assert(fd >= 0);
  int value;
  socklen_t value_len = sizeof(value);
  int r = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, &value_len);
  if (r != 0) {
    perror("getsockeopt");
    return r;
  }
  printf("TCP_NODELAY=%d\n", value); // 0
  poll_loop(&s_server, fd);
  LOG_D("exit\n");
}
