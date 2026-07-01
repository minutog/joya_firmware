#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include "button.h"
#include "app_state.h"

int main(void) {
    printk("Joya firmware booting\n");
    
    int ret = storage_init();
    if (ret != 0) {
        printk("storage_init failed: %d\n", ret);
        return -1;
    }
    printk("storage ready: app_id_empty=%d emergency=%d\n",
           is_app_id_empty(), is_in_emergency());
    
    ret = ble_driver_init();
    if (ret) {
        printk("ble_driver_init failed: %d\n", ret);
        return -1;
    }
    printk("BLE driver ready\n");

    ret = button_init();
    if (ret != 0) {
        printk("button_init failed: %d\n", ret);
        return -1;
    }
    printk("Button ready\n");

    ret = haptics_init();
    if (ret < 0) {
        printk("haptics_init failed: %d; continuing without haptics\n", ret);
    } else {
        printk("Haptics ready\n");
    }

    app_state_start();
    

    // Keep the main thread alive
    k_sleep(K_FOREVER);
}
