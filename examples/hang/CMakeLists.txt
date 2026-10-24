add_executable(hang main.c)
target_link_libraries(hang pico_stdlib)
ota_app(hang)
