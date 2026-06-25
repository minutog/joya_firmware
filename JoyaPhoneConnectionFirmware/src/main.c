#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "button.h"
#include "app_state.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("Starting firmware...");
    
    int ret = storage_init();
    if (ret != 0) {
        LOG_ERR("Error initializing storage: %d", ret);
        return -1;
    }
    
    ret = ble_driver_init();
    if (ret) {
        LOG_ERR("Failed to initialize BLE. Error: %d", ret);
        return -1;
    }

    ret = button_init();
    if (ret != 0) {
        LOG_ERR("Error initializing button: %d", ret);
        return -1;
    }

    ret = haptics_init();
    if (ret < 0) {
        LOG_WRN("Haptics initialization failed: %d", ret);
    }
    
    LOG_INF("Firmware started successfully.");

    // Keep the main thread alive
    k_sleep(K_FOREVER);
}