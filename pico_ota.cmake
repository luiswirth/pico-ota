# Apps include this after pico_sdk_init(), then pass their target to ota_app.

set(PICO_OTA_PATH ${CMAKE_CURRENT_LIST_DIR})

# The flash layout, read from its one place.
file(STRINGS ${PICO_OTA_PATH}/common/layout.env OTA_LAYOUT REGEX "^[A-Z_]+=")
foreach(line ${OTA_LAYOUT})
    string(REGEX MATCH "^([A-Z_]+)=(.*)$" _ ${line})
    set(OTA_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PICO_OTA_PATH}/common/layout.env)
math(EXPR OTA_APP_ORIGIN "0x10000000 + ${OTA_APP_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR OTA_APP_LENGTH "${OTA_DATA_OFFSET} - ${OTA_APP_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)

# Links target into the given part of flash.
function(ota_flash_region target origin length)
    set(dir ${CMAKE_CURRENT_BINARY_DIR}/${target}-region)
    file(WRITE ${dir}/pico_flash_region.ld "FLASH(rx) : ORIGIN = ${origin}, LENGTH = ${length}\n")
    pico_add_linker_script_override_path(${target} ${dir} FILES pico_flash_region.ld)
endfunction()

add_library(ota_common INTERFACE)
target_sources(ota_common INTERFACE
    ${PICO_OTA_PATH}/common/config.c
    ${PICO_OTA_PATH}/common/http.c
    ${PICO_OTA_PATH}/common/link.c)
target_include_directories(ota_common INTERFACE ${PICO_OTA_PATH}/common)
target_compile_definitions(ota_common INTERFACE
    HEADER_OFFSET=${OTA_HEADER_OFFSET}u
    APP_OFFSET=${OTA_APP_OFFSET}u
    DATA_OFFSET=${OTA_DATA_OFFSET}u
    CONFIG_OFFSET=${OTA_CONFIG_OFFSET}u)
target_link_libraries(ota_common INTERFACE pico_stdlib pico_lwip_mdns hardware_watchdog)

add_library(ota_net INTERFACE)
target_sources(ota_net INTERFACE ${PICO_OTA_PATH}/net/net.c)
target_include_directories(ota_net INTERFACE ${PICO_OTA_PATH}/net)
target_link_libraries(ota_net INTERFACE ota_common pico_cyw43_arch_lwip_threadsafe_background
    pico_sync pico_flash hardware_flash)

# Makes target an app: linked for the slot, with the runtime.
# tools/ota flashes its .bin.
function(ota_app target)
    target_link_libraries(${target} ota_net)
    ota_flash_region(${target} ${OTA_APP_ORIGIN} ${OTA_APP_LENGTH})
    pico_add_extra_outputs(${target})
endfunction()
