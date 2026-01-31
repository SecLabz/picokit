cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(picokit C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(picokit
    src/main.c
    src/protocol.c
    src/icsp.c
    src/target.c
)

target_include_directories(picokit PRIVATE src)

target_link_libraries(picokit pico_stdlib hardware_spi)

pico_enable_stdio_usb(picokit 1)
pico_enable_stdio_uart(picokit 0)

pico_add_extra_outputs(picokit)
