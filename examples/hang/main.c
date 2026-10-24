// Test: POST /hang stops everything, so the watchdog has to step in.
#include "hardware/sync.h"
#include "net.h"
#include "pico/stdlib.h"
#include <string.h>
int main(void) {
  stdio_init_all();
  net_init();
  for (;;) {
    net_request req;
    if (!net_wait(&req, 1000))
      continue;
    if (!strcmp(req.path, "/hang")) {
      net_reply(200, "hanging\n", 8);
      sleep_ms(100);
      save_and_disable_interrupts();
      for (;;) {
      }
    }
    net_reply(200, "hang app\n", 9);
  }
}
