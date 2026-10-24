// The device config, kept as text in its own flash sector so that one build
// runs on any network. Lines of key=value: ssid, password, hostname, country.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  char ssid[33];
  char password[65];
  char hostname[33];
  uint32_t country; // CYW43_COUNTRY
} config;

// Reads the config. False if the sector holds no ssid; the rest gets defaults.
bool config_read(config *c);

// Joins the network in c, without waiting. Returns the cyw43 error, 0 on
// success.
int config_connect(const config *c);
