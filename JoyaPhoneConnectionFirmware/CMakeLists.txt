cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED ENV{JOYA_TEST_MODE})
    set(DTS_EXTRA_CPPFLAGS "-DJOYA_TEST_MODE")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(joya)

if(DEFINED ENV{JOYA_TEST_MODE})
    target_compile_definitions(app PRIVATE JOYA_TEST_MODE)
endif()

# Inicialize Zephyr build system
#find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

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
    src/drv2605.c
    src/flash_memory.c
    src/nbm5100.c
)

# Include directories for the application
target_include_directories(app PRIVATE include)
