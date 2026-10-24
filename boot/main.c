// The bootloader. It starts the app in the slot, and takes a new one over
// HTTP: POST /app with the app's .bin as the body.
//
// It listens before starting the app whenever it was not told to start it:
// - after a power-up, for a short window, so a power cycle always gets back in
// - when the app asked for it, until a new app arrives or a timeout
// - after the watchdog caught the app, for longer
// - forever, if the slot holds no valid app
// It never writes its own sectors, so a failed upload can't brick the Pico.
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hardware/flash.h"
#include "hardware/regs/m0plus.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "http.h"
#include "layout.h"
#include "link.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#define POWER_ON_WINDOW_MS 5000 // counted from the moment the link is up
#define POWER_ON_LINK_MS 20000  // give up waiting for the link after this
#define CRASH_WINDOW_MS 30000
#define STAY_WINDOW_MS 60000

static uint32_t crc_table[256];

static void crc_init(void) {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++)
      c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    crc_table[i] = c;
  }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, uint32_t n) {
  crc = ~crc;
  while (n--)
    crc = crc_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
  return ~crc;
}

static const app_header *header(void) {
  return (const app_header *)(FLASH_BASE + HEADER_OFFSET);
}

// The image must be linked for the slot, or jumping to it would crash.
static bool vectors_ok(const uint32_t *vt) {
  uint32_t sp = vt[0], reset = vt[1];
  return sp > SRAM_BASE && sp <= SRAM_END && (reset & 1) && reset > APP_START &&
         reset < APP_START + APP_MAX;
}

static bool app_valid(void) {
  const app_header *h = header();
  if (h->magic != HEADER_MAGIC || h->check != ~(h->magic ^ h->size ^ h->crc))
    return false;
  if (h->size < 0x200 || h->size > APP_MAX)
    return false;
  if (!vectors_ok((const uint32_t *)APP_VECTORS))
    return false;
  return crc_update(0, (const uint8_t *)APP_START, h->size) == h->crc;
}

// Runs the app as the boot ROM would, from the state right after reset.
static void __attribute__((noreturn)) start_app(void) {
  const uint32_t *vt = (const uint32_t *)APP_VECTORS;
  watchdog_enable(WATCHDOG_MS, true); // the app's runtime feeds it
  systick_hw->csr = 0;
  *(volatile uint32_t *)(PPB_BASE + M0PLUS_NVIC_ICER_OFFSET) = ~0u;
  *(volatile uint32_t *)(PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET) = ~0u;
  scb_hw->vtor = (uintptr_t)vt;
  __asm volatile("msr msp, %0\n bx %1\n" : : "r"(vt[0]), "r"(vt[1]));
  __builtin_unreachable();
}

static void __attribute__((noreturn)) reboot(uint32_t reason) {
  watchdog_hw->scratch[BOOT_REASON_SCRATCH] = reason;
  watchdog_reboot(0, 0, 10);
  for (;;)
    tight_loop_contents();
}

// --- Taking a new app ---

static http_conn *upload;    // the connection whose body goes to flash
static uint32_t upload_size; // bytes announced
static uint32_t written;     // bytes in flash so far
static uint32_t upload_crc;
static uint8_t page[FLASH_SECTOR_SIZE];
static uint32_t page_fill;
static absolute_time_t deadline;
static absolute_time_t reboot_at;

static void flash_erase(uint32_t offset, uint32_t size) {
  uint32_t irq = save_and_disable_interrupts();
  flash_range_erase(offset, size);
  restore_interrupts(irq);
}

static void flash_program(uint32_t offset, const uint8_t *data, uint32_t size) {
  uint32_t irq = save_and_disable_interrupts();
  flash_range_program(offset, data, size);
  restore_interrupts(irq);
}

static void flush_page(void) {
  if (!page_fill)
    return;
  memset(page + page_fill, 0xff, sizeof page - page_fill);
  uint32_t offset = APP_OFFSET + written;
  // The old app stays until the new one is known to be linked for the slot.
  if (written == 0)
    flash_erase(HEADER_OFFSET, FLASH_SECTOR_SIZE);
  // Erase a whole 64 KiB block when entering it, which is faster than sectors.
  if (offset % 0x10000 == 0)
    flash_erase(offset, MIN(0x10000u, DATA_OFFSET - offset));
  flash_program(offset, page, sizeof page);
  written += page_fill;
  page_fill = 0;
}

static char reply_buf[160];
static int pending; // what the request being taken is, between begin and end

enum { REQ_STATUS = 1, REQ_APP, REQ_REBOOT };

static int on_begin(http_conn *c, const char *method, const char *path,
                    uint32_t length) {
  bool post = !strcmp(method, "POST");
  if (!strcmp(path, "/")) {
    pending = REQ_STATUS;
    return 0;
  }
  if (!strcmp(path, "/reboot")) {
    if (!post)
      return 405;
    pending = REQ_REBOOT;
    return 0;
  }
  if (!strcmp(path, "/app")) {
    if (!post)
      return 405;
    if (upload)
      return 503;
    if (length < 0x200 || length > APP_MAX)
      return 413;
    printf("boot: taking an app of %lu bytes\n", (unsigned long)length);
    upload = c;
    upload_size = length;
    written = page_fill = 0;
    upload_crc = 0;
    pending = REQ_APP;
    return 0;
  }
  return 404;
}

static int on_body(http_conn *c, const uint8_t *data, uint32_t len) {
  if (c != upload)
    return 400;
  upload_crc = crc_update(upload_crc, data, len);
  while (len) {
    uint32_t n = MIN(len, sizeof page - page_fill);
    memcpy(page + page_fill, data, n);
    page_fill += n;
    data += n;
    len -= n;
    if (page_fill == sizeof page) {
      if (written == 0 && !vectors_ok((const uint32_t *)(page + 0x100))) {
        upload = NULL;
        http_reply(c, 422, "not an app for the slot: build it with ota_app\n",
                   47);
        return -1;
      }
      flush_page();
    }
  }
  deadline = make_timeout_time_ms(STAY_WINDOW_MS);
  return 0;
}

static void on_end(http_conn *c) {
  if (c != upload) {
    if (pending == REQ_REBOOT) {
      http_reply(c, 200, "starting the app\n", 17);
      reboot_at = make_timeout_time_ms(200);
      return;
    }
    const app_header *h = header();
    bool valid = app_valid();
    int n =
        snprintf(reply_buf, sizeof reply_buf,
                 "bootloader\napp: %s, %lu bytes, crc %08lx\n",
                 valid ? "valid" : "none", valid ? (unsigned long)h->size : 0ul,
                 valid ? (unsigned long)h->crc : 0ul);
    http_reply(c, 200, reply_buf, n);
    return;
  }
  upload = NULL;
  if (written == 0 && page_fill &&
      !vectors_ok((const uint32_t *)(page + 0x100)))
    return http_reply(c, 422,
                      "not an app for the slot: build it with ota_app\n", 47);
  flush_page();
  uint32_t crc = crc_update(0, (const uint8_t *)APP_START, upload_size);
  if (crc != upload_crc)
    return http_reply(c, 500, "flash readback mismatch\n", 24);

  static app_header h; // flash_range_program wants a page
  static uint8_t hp[FLASH_PAGE_SIZE];
  h = (app_header){HEADER_MAGIC, upload_size, crc, 0};
  h.check = ~(h.magic ^ h.size ^ h.crc);
  memset(hp, 0xff, sizeof hp);
  memcpy(hp, &h, sizeof h);
  flash_program(HEADER_OFFSET, hp, sizeof hp);

  int n = snprintf(reply_buf, sizeof reply_buf,
                   "flashed %lu bytes, crc %08lx, starting it\n",
                   (unsigned long)upload_size, (unsigned long)crc);
  printf("boot: %s", reply_buf);
  http_reply(c, 200, reply_buf, n);
  reboot_at = make_timeout_time_ms(200);
}

static void on_done(http_conn *c) {
  if (c == upload) {
    printf("boot: upload broke off at %lu bytes\n", (unsigned long)written);
    upload = NULL;
  }
}

static const http_handler handler = {on_begin, on_body, on_end, on_done};

int main(void) {
  uint32_t reason = watchdog_hw->scratch[BOOT_REASON_SCRATCH];
  watchdog_hw->scratch[BOOT_REASON_SCRATCH] = 0;
  bool crashed = reason == 0 && watchdog_enable_caused_reboot();
  crc_init();
  bool valid = app_valid();
  if (reason == BOOT_JUMP && valid)
    start_app();

  stdio_init_all();
  config cfg;
  bool configured = config_read(&cfg);
  printf("boot: reason %s, app %s, config %s\n",
         reason == BOOT_STAY ? "app asked"
         : crashed           ? "watchdog"
                             : "power-up",
         valid ? "valid" : "none", configured ? cfg.ssid : "none");
  if (!configured) {
    if (valid)
      start_app();
    // Nothing to do without a network: wait for USB (picotool works).
    for (;;)
      sleep_ms(1000);
  }
  if (cyw43_arch_init_with_country(cfg.country)) {
    printf("boot: radio failed\n");
    if (valid)
      start_app();
    reboot(0);
  }
  link_init(&cfg);
  http_start(80, &handler);

  // nil_time: no deadline yet, a power-up window starts once the link is up.
  uint32_t window = reason == BOOT_STAY ? STAY_WINDOW_MS
                    : crashed           ? CRASH_WINDOW_MS
                                        : 0;
  deadline = window ? make_timeout_time_ms(window)
                    : make_timeout_time_ms(POWER_ON_LINK_MS);
  bool power_on = !window;
  reboot_at = at_the_end_of_time;
  watchdog_enable(WATCHDOG_MS, true);

  absolute_time_t tick = get_absolute_time();
  bool led = false;
  for (;;) {
    cyw43_arch_poll();
    watchdog_update();
    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(tick, now) > 0) {
      tick = make_timeout_time_ms(250);
      link_tick();
      if (power_on && link_up()) {
        power_on = false;
        deadline = make_timeout_time_ms(POWER_ON_WINDOW_MS);
      }
      cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led = !led);
    }
    if (absolute_time_diff_us(reboot_at, now) > 0)
      reboot(BOOT_JUMP);
    if (!upload && absolute_time_diff_us(deadline, now) > 0) {
      if (app_valid())
        reboot(BOOT_JUMP);
      deadline = at_the_end_of_time; // no app: stay
    }
    cyw43_arch_wait_for_work_until(tick);
  }
}
