#include "ServerApp.h"
#include "Server.h"
#include "impl.h"
#include "picohttpparser.h"
#include "support.h"
#include <assert.h>
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
#if 0
static const char OK_CHUNKED_RESPONSE[79] =
    OK_LINE ALLOW_CORS_HEADER TRANSFER_ENCODING_CHUNKED_HEADER "\r\n";
#endif
// 到Content-Length: 处，不含数值：长度65
#define OK_CONTENT_LENGTH_LEN 65
static const char OK_CONTENT_LENGTH_RESPONSE[70] =
    OK_LINE ALLOW_CORS_HEADER CONTENT_LENGTH_HEADER "0\r\n\r\n";
#if 0
#define BAD_REQUEST_CONTENT_LENGTH_LEN 74
static const char BAD_REQUEST_CONTENT_LENGTH_RESPONSE[79] =
    BAD_REQUEST_LINE ALLOW_CORS_HEADER CONTENT_LENGTH_HEADER "0\r\n\r\n";

static const char NOT_FOUND_RESPONSE[77] =
    NOT_FOUND_LINE ALLOW_CORS_HEADER CONTENT_LENGTH_HEADER "0\r\n\r\n";
#endif
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

static void process_get_request(Connection *c, SpanConstChar path) {
  if (is_version(path)) {
    const char *p = "1.1.0 " __DATE__ " " __TIME__;
    send_ok_response(c, p, strlen(p));
  }
#if 0
	else if (is_periodic(path)) {
	  send_ok_chunked_response(c);
	  c->http.state = kWaitSending;
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
#endif
}

static int ProcessHead(Connection *c, SpanConstChar method, SpanConstChar path,
                       struct phr_header headers[MAX_HDR_NUM],
                       size_t num_headers, size_t head_len) {
  (void)path;
  (void)headers;
  (void)num_headers;
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
  }
#if 0
  else if (is_post(method)) {
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
  }
#endif
  else {
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
static void ClearRecvBuf(Connection *c) { c->recvIdx = 0; }
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
    LOG_W("Not implemented\n");
    // LOG_D("recv %zu in body state\n", c->recvIdx);
    // rst = on_recv_body(c);
    // if (rst == RC_ERR) {
    //   doClose=true;
    // }
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
