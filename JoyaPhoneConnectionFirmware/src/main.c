#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include "button.h"
#include "app_state.h"
#include "nbm5100.h"

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
        ble_start_setup_advertising(true);
        return -1;
    }

    nbm_ready_init();
    ret = haptics_init();
    if (ret < 0) {
        // LOG: Haptics initialization failed - continuing without haptics
        // (improvement): decide what to do if haptics initialization fails (e.g., retry, log error, etc.)
    } else {
        /*
         * Non-blocking startup check. If the haptic controller was detected,
         * schedule one short existing pattern without delaying BLE or OTA.
         */
        // Removed because haptic_ready_work is now inicialized in fsm_thread_loop()
        // haptics_play_when_ready(HAPTICS_PATTERN_SETUP_MODE);
    }
    
    // LOG: Initialization complete


    // Keep the main thread alive
    k_sleep(K_FOREVER);
}
