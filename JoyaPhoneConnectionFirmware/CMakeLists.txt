cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Inicialize Zephyr build system
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Project name and version
project(
    joya_phone_connection_fw
    VERSION 2.0
    DESCRIPTION "Firmware for Joya controller with modular architecture"
)

# Source files for the application
target_sources(app PRIVATE 
    src/main.c
    src/app_comm.c
    src/app_state.c
    src/ble_driver.c
    src/button.c
    src/haptics.c
    src/flash_memory.c
)

# Include directories for the application
target_include_directories(app PRIVATE include)