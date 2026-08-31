#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include "button.h"
#include "app_state.h"
#include "debug_rtt.h"

int main(void) {
    bool ota_test_boot = !boot_is_img_confirmed();
    
    int ret ;
    ret = storage_init();
    if (ret != 0) {
        // LOG: Storage initialization failed - continuing with RAM defaults
        // (improvement): decide what to do if haptics initialization fails (e.g., retry, log error, etc.)
    }
    
    ret = ble_driver_init();
    if (ret) {
        // LOG: BLE driver initialization failed
        return -1;
    }

    if (ota_test_boot) {
        ble_start_setup_advertising(true);
    }
    
    ret = button_init();
    if (ret != 0) {
        // LOG: Button initialization failed
        return -1;
    }


    ret = haptics_init();
    if (ret < 0) {
        // LOG: Haptics initialization failed - continuing without haptics
        // (improvement): decide what to do if haptics initialization fails (e.g., retry, log error, etc.)
    }
    
    // LOG: Initialization complete


    // Keep the main thread alive
    k_sleep(K_FOREVER);
}