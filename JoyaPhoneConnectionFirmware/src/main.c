#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "button.h"
#include "app_state.h"
#include "debug_rtt.h"

int main(void) {
    
    int ret = storage_init();
    if (ret != 0) {
        dbg_rtt_mark("Storage initialization failed - continuing with RAM defaults\n");
        // (improvement): decide what to do if haptics initialization fails (e.g., retry, log error, etc.)
    }
    
    ret = ble_driver_init();
    if (ret) {
        dbg_rtt_mark("BLE driver initialization failed\n");
        return -1;
    }

    ret = button_init();
    if (ret != 0) {
        dbg_rtt_mark("Button initialization failed\n");
        return -1;
    }

    ret = haptics_init();
    if (ret < 0) {
        dbg_rtt_mark("Haptics initialization failed - continuing without haptics\n");
        // (improvement): decide what to do if haptics initialization fails (e.g., retry, log error, etc.)
    }
    
    dbg_rtt_mark("Initialization complete\n");

    // Keep the main thread alive
    k_sleep(K_FOREVER);
}