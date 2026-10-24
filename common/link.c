#include "link.h"

#include <stdio.h>

#include "lwip/apps/mdns.h"
#include "pico/cyw43_arch.h"

#define RETRY_MS 10000
#define JOIN_TIMEOUT_MS 30000

static const config *cfg;
static absolute_time_t attempt;
static int last_status = 99;

static void join(void) {
  attempt = get_absolute_time();
  int err = config_connect(cfg);
  if (err)
    printf("link: join failed to start (%d)\n", err);
}

void link_init(const config *c) {
  cfg = c;
  cyw43_arch_enable_sta_mode();
  struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
  cyw43_arch_lwip_begin();
  netif_set_hostname(n, c->hostname);
  mdns_resp_init();
  mdns_resp_add_netif(n, c->hostname);
  mdns_resp_add_service(n, c->hostname, "_http", DNSSD_PROTO_TCP, 80, NULL,
                        NULL);
  cyw43_arch_lwip_end();
  join();
}

void link_tick(void) {
  int s = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  if (s != last_status) {
    if (s == CYW43_LINK_UP)
      printf("link: up as %s.local, %s\n", cfg->hostname,
             ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])));
    else
      printf("link: status %d\n", s);
    last_status = s;
  }
  if (s == CYW43_LINK_UP)
    return;
  int64_t since = absolute_time_diff_us(attempt, get_absolute_time()) / 1000;
  bool failed = s == CYW43_LINK_DOWN || s < 0;
  if ((failed && since > RETRY_MS) || since > JOIN_TIMEOUT_MS)
    join();
}

bool link_up(void) {
  return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}
