#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include "button.h"
#include "app_state.h"
#include "battery.h"

int main(void) {
    bool ota_test_boot = !boot_is_img_confirmed();

    int ret ;
    ret = storage_init();
    if (ret != 0) {
        // Storage initialization failed - continuing with RAM defaults
    }
    
    ret = ble_driver_init();
    if (ret) {
        // BLE driver initialization failed
        return -1;
    }

    if (ota_test_boot) {
        ble_start_setup_advertising(true);
    }
    
    ret = button_init();
    if (ret != 0) {
        // Button initialization failed
        return -1;
    }


    ret = haptics_init();
    if (ret < 0) {
        // LOG: Haptics initialization failed - continuing without haptics
        // (improvement): decide what to do if haptics initialization fails (e.g., retry, log error, etc.)
    } 
    
    /*else {
         * Non-blocking startup check. If the haptic controller was detected,
         * schedule one short existing pattern without delaying BLE or OTA.
         
        haptics_play(HAPTICS_PATTERN_ACK_CONNECTION);
    }
    */

    ret = battery_init();
    if (ret == 0) {
        ret = battery_monitor_start();
    }
    
    // LOG: Initialization complete


    // Keep the main thread alive
    k_sleep(K_FOREVER);
}
