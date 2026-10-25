# pico-ota

A Wi-Fi bootloader and app runtime for the Raspberry Pi Pico W, on the Pico SDK.

- `boot/` is the bootloader, `net/` the runtime every app links, `common/` what both share.
- `common/layout.env` sets the flash layout, `common/layout.h` says what each part is for and holds the hand-over between bootloader and app.
- An app is a CMake target passed to `ota_app` from `pico_ota.cmake`, which an app's own repository includes; `examples/` has some.
- `tools/ota` flashes an app over Wi-Fi and writes the config over USB; the `justfile` wraps it.

## Invariants

- The bootloader never writes its own sectors, so nothing sent over Wi-Fi can brick the Pico:
  It goes on over USB only.
- Every app links the runtime:
  The bootloader arms the watchdog before it starts an app, and the runtime feeds it.
- The Wi-Fi config lives on the device, in its own flash sector, never in a build.
