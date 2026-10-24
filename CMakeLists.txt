cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w)
# For clangd, which finds it in build/.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_ota C CXX ASM)
pico_sdk_init()

# The flash layout, as offsets into the 2 MiB flash (see common/layout.h).
set(OTA_HEADER_OFFSET 0x7f000)
set(OTA_APP_OFFSET 0x80000)
set(OTA_CONFIG_OFFSET 0x1ff000)
math(EXPR OTA_APP_ORIGIN "0x10000000 + ${OTA_APP_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR OTA_APP_LENGTH "${OTA_CONFIG_OFFSET} - ${OTA_APP_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR OTA_CONFIG_ADDR "0x10000000 + ${OTA_CONFIG_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
# For tools/ota, which writes the config over USB.
file(WRITE ${CMAKE_BINARY_DIR}/layout.env "CONFIG_ADDR=${OTA_CONFIG_ADDR}\n")

# Links target into the given part of flash.
function(ota_flash_region target origin length)
    set(dir ${CMAKE_CURRENT_BINARY_DIR}/${target}-region)
    file(WRITE ${dir}/pico_flash_region.ld "FLASH(rx) : ORIGIN = ${origin}, LENGTH = ${length}\n")
    pico_add_linker_script_override_path(${target} ${dir} FILES pico_flash_region.ld)
endfunction()

add_library(ota_common INTERFACE)
target_sources(ota_common INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/common/config.c
    ${CMAKE_CURRENT_LIST_DIR}/common/http.c
    ${CMAKE_CURRENT_LIST_DIR}/common/link.c)
target_include_directories(ota_common INTERFACE ${CMAKE_CURRENT_LIST_DIR}/common)
target_compile_definitions(ota_common INTERFACE
    HEADER_OFFSET=${OTA_HEADER_OFFSET}u
    APP_OFFSET=${OTA_APP_OFFSET}u
    CONFIG_OFFSET=${OTA_CONFIG_OFFSET}u)
target_link_libraries(ota_common INTERFACE pico_stdlib pico_lwip_mdns hardware_watchdog)

add_executable(boot boot/main.c)
target_link_libraries(boot ota_common pico_cyw43_arch_lwip_poll hardware_flash)
pico_enable_stdio_usb(boot 1)
pico_enable_stdio_uart(boot 0)
ota_flash_region(boot 0x10000000 ${OTA_HEADER_OFFSET})
pico_add_extra_outputs(boot)

add_library(ota_net INTERFACE)
target_sources(ota_net INTERFACE ${CMAKE_CURRENT_LIST_DIR}/net/net.c)
target_include_directories(ota_net INTERFACE ${CMAKE_CURRENT_LIST_DIR}/net)
target_link_libraries(ota_net INTERFACE ota_common pico_cyw43_arch_lwip_threadsafe_background pico_sync)

# Makes target an app: linked for the slot, with the runtime.
# tools/ota flashes its .bin.
function(ota_app target)
    target_link_libraries(${target} ota_net)
    ota_flash_region(${target} ${OTA_APP_ORIGIN} ${OTA_APP_LENGTH})
    pico_add_extra_outputs(${target})
endfunction()

add_subdirectory(examples/blink)
add_subdirectory(examples/hang)
