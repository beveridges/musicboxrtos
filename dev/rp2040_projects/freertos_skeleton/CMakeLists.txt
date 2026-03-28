cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../pico-sdk")
set(PICO_BOARD waveshare_rp2040_zero)
set(PICO_NO_PICOTOOL 1)

# Avoid building CYW43 (WiFi) host tool pioasm — requires host C++ compiler (often missing when building under cmd).
# This project does not use WiFi.
set(PICO_CYW43_DRIVER_PATH "${CMAKE_CURRENT_LIST_DIR}/.no_cyw43")

include(pico_sdk_import.cmake)

project(freertos_skeleton C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# FreeRTOS kernel (RP2040 SMP port) — include library.cmake after pico_sdk_init (no second project)
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/../../FreeRTOS-Kernel)
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/library.cmake)

add_executable(freertos_skeleton
    src/main.c
    src/gpio_led.c
    src/midi_task.c
    src/menu.c
    src/ui_task.c
    src/display_task.c
    src/pot_task.c
    src/usb_descriptors.c
    src/usb_task.c
    src/pcd8544.c
)

target_include_directories(freertos_skeleton PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(freertos_skeleton
    pico_stdlib
    hardware_adc
    pico_unique_id
    tinyusb_device
    FreeRTOS-Kernel-Static
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

pico_add_extra_outputs(freertos_skeleton)

pico_enable_stdio_usb(freertos_skeleton 0)
pico_enable_stdio_uart(freertos_skeleton 1)