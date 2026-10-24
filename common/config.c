#include "config.h"

#include <string.h>

#include "layout.h"
#include "pico/cyw43_arch.h"

static void copy_value(char *dst, size_t cap, const char *src, size_t len) {
  if (len >= cap)
    len = cap - 1;
  memcpy(dst, src, len);
  dst[len] = 0;
}

bool config_read(config *c) {
  memset(c, 0, sizeof *c);
  strcpy(c->hostname, "pico");
  c->country = CYW43_COUNTRY_WORLDWIDE;

  const char *p = (const char *)(FLASH_BASE + CONFIG_OFFSET);
  const char *end = p + 4096;
  while (p < end && *p && *p != (char)0xff) {
    const char *eol = p;
    while (eol < end && *eol && *eol != '\n' && *eol != (char)0xff)
      eol++;
    const char *eq = memchr(p, '=', eol - p);
    if (eq) {
      size_t klen = eq - p;
      const char *v = eq + 1;
      size_t vlen = eol - v;
      if (vlen && v[vlen - 1] == '\r')
        vlen--;
      if (klen == 4 && !memcmp(p, "ssid", 4))
        copy_value(c->ssid, sizeof c->ssid, v, vlen);
      else if (klen == 8 && !memcmp(p, "password", 8))
        copy_value(c->password, sizeof c->password, v, vlen);
      else if (klen == 8 && !memcmp(p, "hostname", 8))
        copy_value(c->hostname, sizeof c->hostname, v, vlen);
      else if (klen == 7 && !memcmp(p, "country", 7) && vlen == 2)
        c->country = CYW43_COUNTRY(v[0], v[1], 0);
    }
    p = eol + 1;
  }
  return c->ssid[0] != 0;
}

int config_connect(const config *c) {
  uint32_t auth = c->password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
  return cyw43_arch_wifi_connect_async(
      c->ssid, c->password[0] ? c->password : NULL, auth);
}
