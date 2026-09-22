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

    
    int nbm_rdy_err = nbm_ready_init();
    int nbm_err = nbm5100_init();
    int drv_err = haptics_init();

    bool all_initialized = nbm_rdy_err == 0 && nbm_err == 0 && drv_err == 0;
    haptics_set_available(all_initialized);

    // Error treatment (TO DO)

    
    // LOG: Initialization complete


    // Keep the main thread alive
    k_sleep(K_FOREVER);
}
