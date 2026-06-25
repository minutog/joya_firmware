#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "button.h"
#include "app_state.h"

int main(void) {
    
    int ret = storage_init();
    if (ret != 0) {
        return -1;
    }
    
    ret = ble_driver_init();
    if (ret) {
        return -1;
    }

    ret = button_init();
    if (ret != 0) {
        return -1;
    }

    ret = haptics_init();
    if (ret < 0) {
        // (improvement): decide what to do if haptics initialization fails (e.g., retry, log error, etc.)
    }
    

    // Keep the main thread alive
    k_sleep(K_FOREVER);
}