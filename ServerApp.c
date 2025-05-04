#include "ServerApp.h"
#include "Buffer.h"
#include "Server.h"
#include "impl.h"
#include "picohttpparser.h"
#include "support.h"
#include <assert.h>
#include <ctype.h>
#include <string.h>
#define RC_OK (0)
#define RC_ERR (-1)
#define RC_INCOMPLETE (-2)
#define MAX_HDR_NUM (20)
#define HTTP_VERSION "HTTP/1.1 "
#define OK_LINE HTTP_VERSION "200 OK\r\n"
#define BAD_REQUEST_LINE HTTP_VERSION "400 Bad Request\r\n"
#define NOT_FOUND_LINE HTTP_VERSION "404 Not Found\r\n"
#define ALLOW_CORS_HEADER "Access-Control-Allow-Origin: *\r\n"
#define TRANSFER_ENCODING_CHUNKED_HEADER "Transfer-Encoding: chunked\r\n"
#define CONTENT_LENGTH_HEADER "Content-Length: "

// TODO 缩短下名字，使得容易理解
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

void AppOnPeerClose(Connection *c) { MyClose(c); }
void AppOnError(Connection *c) { MyClose(c); }
bool AppOnAccepting(Server *server, const struct sockaddr_storage *addr,
                    socklen_t addrLen) {
  (void)server;
  (void)addr;
  (void)addrLen;
  return true;
}
void AppOnAccepted(Connection *c) { SetupToRecv(c, RECV_BUF_LEN, 0); }
void AppOnSend(Connection *c) { (void)c; }
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
static void send_content_length(Connection *c, size_t len) {
  assert(len <= 9999);
  char s[8];
  size_t l = num_to_four_chars((int)len, s);
  assert(l <= 4);
  memcpy(&s[l], "\r\n\r\n", 4);
  AddToSendBuffer(c, s, l + 4);
}
// HTTP 200
static void send_ok_response(Connection *c, const void *body,
                             size_t body_len) { // 200
  assert(body_len <= 9999);                     // 最大支持4 digits
  if (body_len > 0) {
    AddToSendBuffer(c, OK_CONTENT_LENGTH_RESPONSE, OK_CONTENT_LENGTH_LEN);
    send_content_length(c, body_len);
    AddToSendBuffer(c, body, body_len);
  } else {
    AddToSendBuffer(c, OK_CONTENT_LENGTH_RESPONSE,
                    sizeof(OK_CONTENT_LENGTH_RESPONSE));
  }
}
static void send_chunk_with_data(Connection *c, const void *chunk, int len) {
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
  AddToSendBuffer(c, length_line, line_len);
  AddToSendBuffer(c, chunk, (size_t)len);
  AddToSendBuffer(c, "\r\n", 2);
}

static uint32_t g_tick = 0;
typedef struct {
  uint32_t param1;
  uint32_t param2;
} Param;
static Param g_param = {0x12345678, 0x90abcdef};

static void send_ok_chunked_response(Connection *c) {
  AddToSendBuffer(c, OK_CHUNKED_RESPONSE, sizeof(OK_CHUNKED_RESPONSE));
  uint32_t t = htonl(g_tick);
  send_chunk_with_data(c, &t, 4);
  c->server->lastSendTick = xTaskGetTickCount();
}
// HTTP 400
static void send_bad_request_response(Connection *c, const void *body,
                                      size_t body_len) {
  assert(body_len <= 9999); // 最大支持4 digits
  if (body_len > 0) {
    AddToSendBuffer(c, BAD_REQUEST_CONTENT_LENGTH_RESPONSE,
                    BAD_REQUEST_CONTENT_LENGTH_LEN);
    send_content_length(c, body_len);
    AddToSendBuffer(c, body, body_len);
  } else {
    AddToSendBuffer(c, BAD_REQUEST_CONTENT_LENGTH_RESPONSE,
                    sizeof(BAD_REQUEST_CONTENT_LENGTH_RESPONSE));
  }
}
static void on_get_param(Connection *c, uint16_t addr, uint16_t len) {
  if ((len > 0) && (addr + len <= sizeof(Param))) {
    char *p = (char *)&g_param;
    send_ok_response(c, &p[addr], len);
  } else {
    send_bad_request_response(c, NULL, 0);
  }
}
static void process_get_request(Connection *c, SpanConstChar path) {
  if (is_version(path)) {
    const char *p = "1.1.0 " __DATE__ " " __TIME__;
    send_ok_response(c, p, strlen(p));
  } else if (is_periodic(path)) {
    send_ok_chunked_response(c);
    c->http.state = kWaitSending;
  } else {
    uint16_t addr;
    uint16_t len;
    if (is_get_param(path, &addr, &len)) { // 可能路径OK但参数误应该报另外的错
      on_get_param(c, addr, len);
    } else {
      // 404
      AddToSendBuffer(c, NOT_FOUND_RESPONSE, sizeof(NOT_FOUND_RESPONSE));
    }
  }
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
#define MIN_IMAGE_SIZE (1)
#define MAX_IMAGE_SIZE (100)
static void on_post_image(Connection *c, const unsigned char *body,
                          int32_t content_len) {
  LOG_D("POST /image %d", content_len);
  for (int32_t i = 0; i < content_len; ++i) {
    LOG_D(" %02x", body[i]);
  }
  LOG_D("\n");
  if (content_len >= MIN_IMAGE_SIZE && content_len <= MAX_IMAGE_SIZE) {
    AddToSendBuffer(c, OK_CONTENT_LENGTH_RESPONSE,
                    sizeof(OK_CONTENT_LENGTH_RESPONSE));
  } else {
    AddToSendBuffer(c, BAD_REQUEST_CONTENT_LENGTH_RESPONSE,
                    sizeof(BAD_REQUEST_CONTENT_LENGTH_RESPONSE));
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
    on_post_image(c, body, content_len);
  } else {
    // 404
    AddToSendBuffer(c, NOT_FOUND_RESPONSE, sizeof(NOT_FOUND_RESPONSE));
  }
}
static int ProcessHead(Connection *c, SpanConstChar method, SpanConstChar path,
                       struct phr_header headers[MAX_HDR_NUM],
                       size_t num_headers, size_t head_len) {
  if (is_get(method)) {
    // process and send response
    // for simplicity, discard extra data
    // (or close connection?)
    // GET with body is not supported
    // more than one request at a time is not supported
    if (c->recvIdx > head_len) {
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
    } else if ((size_t)content_len <= (c->recvIdx - head_len)) {
      // can be processed
      // 索性应用层不用header
      process_post_request(c, path, &c->recvBuf[head_len], content_len);
      return RC_OK;
    } else {
      // if is /image, copy data to image buffer, store future data there
      // 怎么存放header (不用存，用个flag知道是/image即可)
      // 是否要为body另开一个缓存？
      c->http.state = kReceivingBody;
      c->http.content_len = content_len;
      return RC_INCOMPLETE;
    }
  } else {
    // only PATCH, POST, and PUT requests have a body
    // send error response, or wait for complete request? or close?
    // for simplicity, just close
    return RC_ERR;
  }
}
static int OnRecvHead(Connection *c) {
  SpanConstChar method;
  SpanConstChar path;
  int minor_version;
  struct phr_header headers[MAX_HDR_NUM];
  size_t num_headers = MAX_HDR_NUM;
  // picohttpparser可以对一个stream反复调用 (但基本都要从头开始parse)
  int pret = phr_parse_request(
      (const char *)c->recvBuf, c->recvIdx, &method.buf, &method.len, &path.buf,
      &path.len, &minor_version, headers, &num_headers, c->http.prevbuflen);
  LOG_D("phr_parse=%d\n", pret);
  if (pret > 0) {
    // if err, close
    // if body incomplete, keep receving
    // if ok, recv next
    int r = ProcessHead(c, method, path, headers, num_headers, (size_t)pret);
    switch (r) {
    case RC_OK:
      return RC_OK;
    case RC_INCOMPLETE: // head is complete while full request not
      c->http.state = kReceivingBody;
      return RC_INCOMPLETE;
    default: // RC_ERR
      return r;
    }
  } else if (pret == -2) { // incomplete
    c->http.prevbuflen = c->recvIdx;
    return RC_INCOMPLETE;
  } else {
    LOG_W("header parse err\n");
    return RC_ERR;
  }
}
// 抽象recv buf, send buf，改成其方法
static void ClearRecvBuf(Connection *c) {
  c->recvIdx = 0;
  c->toRecv = RECV_BUF_LEN;
  // toRecv should be as large as possible for HTTP
}
static int on_recv_body(Connection *c) {
  if (c->recvIdx < (size_t)c->http.content_len) {
    return RC_INCOMPLETE;
  }
  // 收满body，可以处理
  // 根据on_recv_head的实现，只会是POST (header数据存到哪里？)
  return RC_OK;
}
void AppOnRecv(Connection *c) {
  int rst;
  bool doClose = false;
  switch (c->http.state) {
  case kReceivingHead:
    LOG_D("recv %zu in head state\n", c->recvIdx);
    rst = OnRecvHead(c);
    switch (rst) {
    case RC_ERR:
      doClose = true;
      break;
    case RC_OK:
      ClearRecvBuf(c);
      break;
    }
    break;
  case kReceivingBody:
    LOG_D("recv %zu in body state\n", c->recvIdx);
    rst = on_recv_body(c);
    if (rst == RC_ERR) {
      doClose = true;
    }
    break;
  default: // 其他状态都不该收到数据
    LOG_E("recv in state %d\n", c->http.state);
    doClose = true;
    break;
  }
  if (doClose) {
    MyClose(c);
  }
}
void AppOnPollTimeout(Server *server) {
  for (FlNodeBase *it = FL_Begin(&server->connections.base);
       it != FL_End(&server->connections.base); it = it->next) {
    Connection *c = &((ListNodeConnection *)it)->value;
    if (c->http.state == kWaitSending) {
      ++g_tick;
      uint32_t t = htonl(g_tick);
      send_chunk_with_data(c, &t, 4);
      // TODO do not update if send fail
      server->lastSendTick = xTaskGetTickCount();
      // if (g_tick > 5) {
      //   send_empty_chunk(server->conn[i].fd);
      //   server->conn[i].state = kReceiving;
      // }
    }
  }
}
static const uint16_t PERIOD_MS = 1000;
// 是否存在监听周期数据的client
static bool has_listening_client(Server *server) {
  for (FlNodeBase *it = FL_Begin(&server->connections.base);
       it != FL_End(&server->connections.base); it = it->next) {
    Connection *c = &((ListNodeConnection *)it)->value;
    if (c->http.state == kWaitSending) {
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
int AppCalcTimeout(Server *server) {
  int timeout;
  if (has_listening_client(server)) {
    timeout = to_next(PERIOD_MS, server->lastSendTick);
  } else {
    timeout = -1;
  }
  return timeout;
}
