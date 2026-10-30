#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

#include "button.h"
#include "app_state.h"

int main(void) {
    const bool ota_test_boot = !boot_is_img_confirmed();
    
    bool critical_error = false;
    bool non_critical_error = false;

    int ret ;
    ret = storage_init();
    if (ret != 0) {
        non_critical_error = true;
    }
    
    ret = ble_driver_init();
    if (ret) {
        critical_error = true;
    }

    if (ota_test_boot) {
        ble_start_setup_advertising(true);
    }
    
    ret = button_init();
    if (ret != 0) {
        critical_error = true;
    }


    ret = haptics_init();
    if (ret < 0) {
        non_critical_error = true;
    } 
    
    if(critical_error){
        if(ota_test_boot){
            // Dejar la imagen sin confirmación, de manera tal que MCUboot pueda revertirla en el próximo reinicio
            sys_reboot(SYS_REBOOT_COLD);
        } else {
            k_sleep(K_FOREVER);
            // Provisorio
        }
    }

    if(ota_test_boot){
        ret = boot_write_img_confirmed();
        if(ret != 0){
            // Fallo en la confirmación -> rollback
            sys_reboot(SYS_REBOOT_COLD);
        }
    }

    (void) non_critical_error;

    // Keep the main thread alive
    k_sleep(K_FOREVER);
}
