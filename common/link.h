// Gets the Pico onto the network in its config and keeps it there, answering
// mDNS as <hostname>.local.
#pragma once

#include <stdbool.h>

#include "config.h"

// Starts the join, without waiting, once the radio is up
// (cyw43_arch_init_with_country with c->country).
void link_init(const config *c);

// Call about once a second, in lwIP's context: rejoins after a loss.
void link_tick(void);

bool link_up(void);
