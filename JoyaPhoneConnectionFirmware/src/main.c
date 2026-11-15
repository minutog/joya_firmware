#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>
#include "button.h"
#include "app_state.h"
#include "battery.h"
#include "test_image_guard.h"

int main(void) {
    bool ota_test_boot = !boot_is_img_confirmed();

    int ret ;
    ret = storage_init();
    if (ret != 0) {
        // Storage initialization failed - continuing with RAM defaults
    }
    
    if (ota_test_boot) {
        /* A Test image must prove the recovery button and BLE setup path work. */
        test_image_guard_init();
        ret = button_init();
        if (ret != 0) {
            sys_reboot(SYS_REBOOT_COLD);
        }
        ret = ble_driver_init();
        if (ret != 0) {
            sys_reboot(SYS_REBOOT_COLD);
        }
        ret = app_start_test_setup();
        if (ret != 0) {
            sys_reboot(SYS_REBOOT_COLD);
        }
        test_image_guard_start();
    } else {
        ret = ble_driver_init();
        if (ret != 0) {
            // BLE driver initialization failed
            return -1;
        }
        ret = button_init();
        if (ret != 0) {
            // Button initialization failed
            return -1;
        }
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
