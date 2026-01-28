cmake_minimum_required(VERSION 3.13)

# Pull in PICO SDK (must be before project)
include(pico_sdk_import.cmake)

project(dmg_composite C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

add_executable(dmg_composite
    src/gb_composite_simple.c
)

# No longer using PIO - using PWM instead for simpler composite output
# pico_generate_pio_header(dmg_composite ${CMAKE_CURRENT_LIST_DIR}/src/composite.pio)

target_include_directories(dmg_composite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# We need tight timing, so overclock to 270MHz
target_compile_definitions(dmg_composite PRIVATE
    PICO_FLASH_SPI_CLKDIV=4
)

target_link_libraries(dmg_composite PRIVATE
    pico_stdlib
    pico_multicore
    hardware_pwm
    hardware_clocks
)

# Enable USB output for debugging (optional)
pico_enable_stdio_usb(dmg_composite 1)
pico_enable_stdio_uart(dmg_composite 0)

# Create UF2 file for easy flashing
pico_add_extra_outputs(dmg_composite)
