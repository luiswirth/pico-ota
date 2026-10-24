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

// Flash the app owns, kept across app updates and power loss: NET_DATA_SIZE
// bytes, read straight from NET_DATA.
#define NET_DATA ((const uint8_t *)(0x10000000u + DATA_OFFSET))
#define NET_DATA_SIZE (CONFIG_OFFSET - DATA_OFFSET)

// Writes len bytes at offset into the data, erasing the 4 KiB sectors they
// touch: offset must be a multiple of 4096. False if it doesn't fit.
bool net_data_write(uint32_t offset, const void *data, uint32_t len);

// Waits for the next request. Its fields stay valid until net_reply.
bool net_wait(net_request *req, uint32_t timeout_ms);

// Answers the request net_wait returned, with a body of any length. Returns
// once it is sent, or the client is gone.
void net_reply(int status, const void *body, uint32_t length);
