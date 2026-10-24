// Blinks the LED. POST /period with a number of milliseconds changes the pace.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

int main(void) {
  stdio_init_all();
  net_init();
  uint32_t period = 500;
  bool led = false;
  for (;;) {
    net_request req;
    if (net_wait(&req, period)) {
      char reply[64];
      if (!strcmp(req.path, "/period") && !strcmp(req.method, "POST")) {
        char num[16] = {0};
        memcpy(num, req.body, req.length < 15 ? req.length : 15);
        uint32_t p = strtoul(num, NULL, 10);
        if (p >= 20 && p <= 10000) {
          period = p;
          int n = snprintf(reply, sizeof reply, "period %lu ms\n",
                           (unsigned long)period);
          net_reply(200, reply, n);
        } else {
          net_reply(422, "period must be 20 to 10000 ms\n", 30);
        }
      } else if (!strcmp(req.path, "/")) {
        int n = snprintf(reply, sizeof reply, "blink, period %lu ms\n",
                         (unsigned long)period);
        net_reply(200, reply, n);
      } else {
        net_reply(404, "not found\n", 10);
      }
      continue;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led = !led);
  }
}
