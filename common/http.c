#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lwip/tcp.h"

#define CONNS 4
#define HEAD_MAX 1024
#define IDLE_POLLS 40 // of 0.5 s

enum state { FREE, HEAD, BODY, WAIT, DONE };

struct http_conn {
  struct tcp_pcb *pcb; // NULL once lwIP is done with it
  enum state state;
  uint8_t idle;
  uint16_t head_len;
  uint32_t remaining;
  char head[HEAD_MAX];
};

static http_conn conns[CONNS];
static const http_handler *handler;
// Set when a pcb was aborted, which a callback must report as ERR_ABRT.
static bool aborted;

static const char *reason(int status) {
  switch (status) {
  case 100:
    return "Continue";
  case 200:
    return "OK";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 411:
    return "Length Required";
  case 413:
    return "Content Too Large";
  case 422:
    return "Unprocessable Content";
  case 431:
    return "Request Header Fields Too Large";
  case 500:
    return "Internal Server Error";
  case 503:
    return "Service Unavailable";
  default:
    return "";
  }
}

// Frees a connection that ends without a reply.
static void drop(http_conn *c) {
  if (c->state == BODY)
    handler->gone(c);
  c->state = FREE;
}

// Lets go of the pcb. Returns true if it had to be aborted.
static bool detach(http_conn *c) {
  struct tcp_pcb *pcb = c->pcb;
  if (!pcb)
    return false;
  c->pcb = NULL;
  tcp_arg(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_err(pcb, NULL);
  tcp_poll(pcb, NULL, 0);
  if (tcp_close(pcb) != ERR_OK) {
    tcp_abort(pcb);
    aborted = true;
    return true;
  }
  return false;
}

void http_reply(http_conn *c, int status, const char *body, uint32_t len) {
  if (c->pcb) {
    char head[128];
    int n = snprintf(
        head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
        status, reason(status), (unsigned long)len);
    tcp_write(c->pcb, head, n,
              TCP_WRITE_FLAG_COPY | (len ? TCP_WRITE_FLAG_MORE : 0));
    if (len)
      tcp_write(c->pcb, body, len, TCP_WRITE_FLAG_COPY);
    tcp_output(c->pcb);
    detach(c);
  }
  c->state = FREE;
}

static void reply_status(http_conn *c, int status) {
  const char *r = reason(status);
  char body[64];
  int n = snprintf(body, sizeof body, "%d %s\n", status, r);
  http_reply(c, status, body, n);
}

// Parses the header in c->head. Returns 0 or an error status.
static int parse_head(http_conn *c) {
  char *method = c->head;
  char *sp1 = strchr(method, ' ');
  if (!sp1)
    return 400;
  *sp1 = 0;
  char *path = sp1 + 1;
  char *sp2 = strchr(path, ' ');
  if (!sp2)
    return 400;
  *sp2 = 0;

  long length = 0;
  bool expect_continue = false;
  for (char *line = strstr(sp2 + 1, "\r\n"); line && line[2];
       line = strstr(line + 2, "\r\n")) {
    char *l = line + 2;
    if (!strncasecmp(l, "content-length:", 15))
      length = strtol(l + 15, NULL, 10);
    else if (!strncasecmp(l, "transfer-encoding:", 18))
      return 411;
    else if (!strncasecmp(l, "expect:", 7) && strstr(l, "100-continue"))
      expect_continue = true;
  }
  if (length < 0)
    return 400;

  c->remaining = (uint32_t)length;
  c->state = BODY;
  int status = handler->begin(c, method, path, c->remaining);
  if (status)
    return status;
  if (expect_continue && c->remaining) {
    static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
    tcp_write(c->pcb, cont, sizeof cont - 1, 0);
    tcp_output(c->pcb);
  }
  return 0;
}

static void feed(http_conn *c, const uint8_t *data, uint32_t len) {
  while (len && (c->state == HEAD || c->state == BODY)) {
    if (c->state == HEAD) {
      if (c->head_len >= HEAD_MAX - 1)
        return reply_status(c, 431);
      c->head[c->head_len++] = (char)*data++;
      len--;
      if (c->head_len >= 4 &&
          !memcmp(c->head + c->head_len - 4, "\r\n\r\n", 4)) {
        c->head[c->head_len - 2] = 0; // keep the last line's \r\n
        int status = parse_head(c);
        if (status > 0)
          reply_status(c, status);
        if (status)
          return;
      } else {
        continue;
      }
    } else {
      uint32_t n = len < c->remaining ? len : c->remaining;
      int status = handler->body(c, data, n);
      if (status > 0)
        reply_status(c, status);
      if (status)
        return;
      c->remaining -= n;
      data += n;
      len -= n;
    }
    if (c->state == BODY && c->remaining == 0) {
      c->state = WAIT;
      handler->end(c);
    }
  }
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p,
                     err_t err) {
  http_conn *c = arg;
  if (!p) {
    // The client closed its side. A pending reply can still go out.
    if (c->state != WAIT) {
      bool aborted = detach(c);
      drop(c);
      return aborted ? ERR_ABRT : ERR_OK;
    }
    return ERR_OK;
  }
  tcp_recved(pcb, p->tot_len);
  c->idle = 0;
  aborted = false;
  for (struct pbuf *q = p; q && c->pcb; q = q->next)
    feed(c, q->payload, q->len);
  pbuf_free(p);
  return aborted ? ERR_ABRT : ERR_OK;
}

static void on_err(void *arg, err_t err) {
  http_conn *c = arg;
  c->pcb = NULL; // lwIP has freed it
  if (c->state != WAIT)
    drop(c);
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
  http_conn *c = arg;
  if (c->state != WAIT && ++c->idle > IDLE_POLLS) {
    bool aborted = detach(c);
    drop(c);
    return aborted ? ERR_ABRT : ERR_OK;
  }
  return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
  if (err != ERR_OK || !pcb)
    return ERR_VAL;
  http_conn *c = NULL;
  for (int i = 0; i < CONNS; i++)
    if (conns[i].state == FREE)
      c = &conns[i];
  if (!c) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  memset(c, 0, offsetof(http_conn, head));
  c->pcb = pcb;
  c->state = HEAD;
  tcp_arg(pcb, c);
  tcp_recv(pcb, on_recv);
  tcp_err(pcb, on_err);
  tcp_poll(pcb, on_poll, 1);
  return ERR_OK;
}

void http_start(uint16_t port, const http_handler *h) {
  handler = h;
  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  tcp_bind(pcb, IP_ANY_TYPE, port);
  pcb = tcp_listen_with_backlog(pcb, CONNS);
  tcp_accept(pcb, on_accept);
}
