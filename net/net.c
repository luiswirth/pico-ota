#include "net.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "http.h"
#include "layout.h"
#include "link.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/sync.h"

#define TICK_MS 500

static config cfg;
static bool online;
static bool to_bootloader;
static async_at_time_worker_t ticker;

// The app's request, from begin until net_reply returns, so that the next one
// waits (503). Touched under the lwIP lock.
static http_conn *app_conn;
static bool app_ready; // its body is in, for net_wait to give
static bool app_has;   // its body is in, so the app answers it
static bool app_over;  // its connection is done, while the app has it
static char method[8], path[128];
static uint8_t body[NET_BODY_MAX];
static uint32_t fill;
static semaphore_t ready;

static void tick(async_context_t *ctx, async_at_time_worker_t *w) {
  // Fed from lwIP's context, so a stuck network stack resets the Pico too.
  watchdog_update();
  if (to_bootloader) {
    watchdog_hw->scratch[BOOT_REASON_SCRATCH] = BOOT_STAY;
    watchdog_reboot(0, 0, 10);
    return;
  }
  if (online)
    link_tick();
  async_context_add_at_time_worker_in_ms(ctx, w, TICK_MS);
}

enum { REQ_APP = 1, REQ_BOOTLOADER };
static int pending;

static int on_begin(http_conn *c, const char *m, const char *p,
                    uint32_t length) {
  if (!strcmp(p, "/bootloader")) {
    if (strcmp(m, "POST"))
      return 405;
    pending = REQ_BOOTLOADER;
    return 0;
  }
  if (app_conn)
    return 503;
  if (length > NET_BODY_MAX)
    return 413;
  if (strlen(m) >= sizeof method || strlen(p) >= sizeof path)
    return 400;
  strcpy(method, m);
  strcpy(path, p);
  app_conn = c;
  app_has = app_over = false;
  fill = 0;
  pending = REQ_APP;
  return 0;
}

static int on_body(http_conn *c, const uint8_t *data, uint32_t len) {
  if (c != app_conn)
    return 400;
  memcpy(body + fill, data, len);
  fill += len;
  return 0;
}

static void on_end(http_conn *c) {
  if (c == app_conn) {
    app_ready = app_has = true;
    sem_release(&ready);
    return;
  }
  http_reply(c, 200, "going to the bootloader\n", 24);
  to_bootloader = true;
}

static void on_done(http_conn *c) {
  if (c != app_conn)
    return;
  if (app_has)
    app_over = true;
  else
    app_conn = NULL;
}

static const http_handler handler = {on_begin, on_body, on_end, on_done};

void net_init(void) {
  // Again, since watchdog_update reloads the time this binary set, not the
  // bootloader's: without it, it reloads 0, which stops the watchdog.
  watchdog_enable(WATCHDOG_MS, true);
  sem_init(&ready, 0, 1);
  bool configured = config_read(&cfg);
  // Without the radio nothing feeds the watchdog, which then restarts the Pico.
  if (cyw43_arch_init_with_country(cfg.country)) {
    printf("net: radio failed\n");
    return;
  }
  if (configured) {
    link_init(&cfg);
    cyw43_arch_lwip_begin();
    http_start(80, &handler);
    cyw43_arch_lwip_end();
    online = true;
  } else {
    printf("net: no config, staying offline\n");
  }
  ticker.do_work = tick;
  async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(), &ticker,
                                         0);
}

bool net_wait(net_request *req, uint32_t timeout_ms) {
  if (!sem_acquire_timeout_ms(&ready, timeout_ms))
    return false;
  cyw43_arch_lwip_begin();
  bool ok = app_ready;
  app_ready = false;
  cyw43_arch_lwip_end();
  if (!ok)
    return false;
  *req = (net_request){method, path, body, fill};
  return true;
}

void net_reply(int status, const void *data, uint32_t length) {
  cyw43_arch_lwip_begin();
  if (!app_has) {
    cyw43_arch_lwip_end();
    return;
  }
  if (!app_over)
    http_reply(app_conn, status, data, length);
  cyw43_arch_lwip_end();
  // The reply goes out from data, as the client takes it.
  for (bool over = false; !over; sleep_ms(1)) {
    cyw43_arch_lwip_begin();
    over = app_over;
    cyw43_arch_lwip_end();
  }
  cyw43_arch_lwip_begin();
  app_conn = NULL;
  app_has = false;
  cyw43_arch_lwip_end();
}

static uint8_t sector[FLASH_SECTOR_SIZE];
static uint32_t sector_offset;

static void write_sector(void *arg) {
  flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
  flash_range_program(sector_offset, sector, FLASH_SECTOR_SIZE);
}

bool net_data_write(uint32_t offset, const void *data, uint32_t len) {
  if (offset % FLASH_SECTOR_SIZE || offset > NET_DATA_SIZE ||
      len > NET_DATA_SIZE - offset)
    return false;
  const uint8_t *p = data;
  for (uint32_t done = 0; done < len; done += FLASH_SECTOR_SIZE) {
    uint32_t n = MIN(len - done, FLASH_SECTOR_SIZE);
    // Through RAM, since data may itself be in flash.
    memset(sector, 0xff, sizeof sector);
    memcpy(sector, p + done, n);
    sector_offset = DATA_OFFSET + offset + done;
    if (flash_safe_execute(write_sector, NULL, 1000) != PICO_OK)
      return false;
  }
  return true;
}
