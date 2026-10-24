// The flash layout and the hand-over between the bootloader and the app.
//
// 0x000000  bootloader, never written by itself
// 0x07f000  app header: which app is in the slot, written last
// 0x080000  app slot, linked there (see app.cmake)
// 0x1ff000  config: Wi-Fi and hostname, as text
#pragma once

#include <stdint.h>

#define FLASH_BASE 0x10000000u
#define FLASH_SIZE (2u * 1024 * 1024)

// HEADER_OFFSET, APP_OFFSET and CONFIG_OFFSET come from CMakeLists.txt,
// which links by them.
#define APP_MAX (CONFIG_OFFSET - APP_OFFSET)

#define APP_START (FLASH_BASE + APP_OFFSET)
// The app keeps the usual layout: 256 bytes of boot2, then the vector table.
#define APP_VECTORS (APP_START + 0x100)

#define HEADER_MAGIC 0x4f544131u // "OTA1"

typedef struct {
  uint32_t magic;
  uint32_t size;  // bytes of the app image
  uint32_t crc;   // crc32 of the image
  uint32_t check; // ~(magic ^ size ^ crc), guards the header itself
} app_header;

// Watchdog scratch 0 tells the bootloader why it was reset. The SDK uses 4
// to 7.
#define BOOT_REASON_SCRATCH 0
#define BOOT_JUMP 0x4a554d50u // start the app at once
#define BOOT_STAY 0x53544159u // the app asked for the bootloader

// How long the app may go without feeding the watchdog.
#define WATCHDOG_MS 8000
