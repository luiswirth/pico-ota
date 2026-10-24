// The runtime every app links. It keeps the Pico on Wi-Fi as <hostname>.local,
// answers HTTP on port 80, and feeds the watchdog the bootloader armed.
// POST /bootloader restarts into the bootloader, to take a new app.
// Every other request goes to the app, one at a time, through net_wait.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Largest request body the app can take.
#ifndef NET_BODY_MAX
#define NET_BODY_MAX (48 * 1024)
#endif

typedef struct {
  const char *method;
  const char *path;
  const uint8_t *body;
  uint32_t length;
} net_request;

// Call first: the watchdog bites a few seconds after the app starts.
void net_init(void);

// Waits for the next request. Its fields stay valid until net_reply.
bool net_wait(net_request *req, uint32_t timeout_ms);

// Answers the request net_wait returned.
void net_reply(int status, const void *body, uint32_t length);
