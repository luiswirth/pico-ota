cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w)
# For clangd, which finds it in build/.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_ota C CXX ASM)
pico_sdk_init()

include(pico_ota.cmake)

add_executable(boot boot/main.c)
target_link_libraries(boot ota_common pico_cyw43_arch_lwip_poll hardware_flash)
pico_enable_stdio_usb(boot 1)
pico_enable_stdio_uart(boot 0)
ota_flash_region(boot 0x10000000 ${OTA_HEADER_OFFSET})
pico_add_extra_outputs(boot)

add_subdirectory(examples/hang)
