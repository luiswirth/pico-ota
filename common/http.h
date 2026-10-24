// A small HTTP/1.1 server on the lwIP raw API, one request per connection.
// The body streams to the handler, so it can be larger than RAM.
// Everything here runs in lwIP's context: under the lwIP lock, or from the
// poll loop.
#pragma once

#include <stdint.h>

typedef struct http_conn http_conn;

typedef struct {
  // The header is in. Return 0 to take the body, a status to answer with,
  // or -1 after answering with http_reply.
  int (*begin)(http_conn *c, const char *method, const char *path,
               uint32_t length);
  // The next piece of the body. Returns as begin does.
  int (*body)(http_conn *c, const uint8_t *data, uint32_t len);
  // The body is complete. The handler answers with http_reply, now or later.
  void (*end)(http_conn *c);
  // The client went away before the body was complete.
  void (*gone)(http_conn *c);
} http_handler;

void http_start(uint16_t port, const http_handler *h);

// Answers and closes. Also fine after the client went away.
void http_reply(http_conn *c, int status, const char *body, uint32_t len);
