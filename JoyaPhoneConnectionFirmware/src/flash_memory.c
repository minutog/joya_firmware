#include "flash_memory.h"
#include <errno.h>
#include <zephyr/sys/printk.h>

bool app_id_loaded_from_flash = false;
uint8_t joya_app_id[SIZE_APP_ID] = {0};
static bool joya_is_in_emergency = false;
static bool invalid_app_id_setting;

const uint8_t* storage_get_app_id(void) {
    return joya_app_id;
}

/**
 * @brief Callback function for settings subsystem to load data from flash
 * @param name The name of the setting being loaded (e.g., "app_id")
 * @param len The length of the data being loaded
 * @param read_cb The callback function to read the data from flash
 * @param cb_arg The argument to pass to the read callback
 * @return 0 on success, negative error code on failure
 * 
 */
static int app_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    // Evaluate APP_ID
    if (settings_name_steq(name, "app_id", &next) && !next) {
        
        if (len != SIZE_APP_ID) {
            printk("Ignoring legacy/invalid APP_ID setting length=%zu expected=%d\n",
                   len, SIZE_APP_ID);
            invalid_app_id_setting = true;
            memset(joya_app_id, 0, SIZE_APP_ID);
            return 0; 
        }

        rc = read_cb(cb_arg, joya_app_id, len);
        if (rc < 0) {
            printk("APP_ID read failed: %d\n", rc);
            return rc;
        }

        if (rc != len) {
            printk("APP_ID short read: %d/%zu\n", rc, len);
            memset(joya_app_id, 0, SIZE_APP_ID);
            return 0;
        }

        app_id_loaded_from_flash = true;
        printk("Loaded APP_ID from flash (%d bytes)\n", SIZE_APP_ID);

        return 0;
    }

    // Evaluate EMERGENCY
    if (settings_name_steq(name, "emergency", &next) && !next) {
        if (len != sizeof(joya_is_in_emergency)) {
            printk("Ignoring invalid emergency setting length=%zu expected=%zu\n",
                   len, sizeof(joya_is_in_emergency));
            joya_is_in_emergency = false;
            return 0;
        }
        rc = read_cb(cb_arg, &joya_is_in_emergency, len);
        if (rc < 0) {
            printk("Emergency state read failed: %d\n", rc);
            return rc;
        }

        if (rc != len) {
            printk("Emergency state short read: %d/%zu\n", rc, len);
            joya_is_in_emergency = false;
            return 0;
        }

        printk("Loaded emergency state from flash: %d\n", joya_is_in_emergency);

        return 0;
    }

    return -ENOENT;
}

/**
 * @brief Check if the device is currently in emergency state
 * @return true if in emergency state, false otherwise
 * 
 */
bool is_in_emergency(void) {
    return joya_is_in_emergency;
}

/**
 * @brief Define a static subnode for settings with the prefix "joya"
 * Every setting that starts with "joya/" will be handled by the app_settings_set function
 */
SETTINGS_STATIC_HANDLER_DEFINE(joya, "joya", NULL, app_settings_set, NULL, NULL);

/**
 * PUBLIC API
 */

int storage_init(void)
{
    // Inicializar el subsistema
    int err = settings_subsys_init();
    if (err) {
        printk("settings_subsys_init failed: %d\n", err);
        return err;
    }

    // Esto dispara la lectura en Flash y llama a tu función 'app_settings_set'
    err = settings_load();
    if (err) {
        printk("settings_load failed: %d\n", err);
        return err;
    }
    
    if (!app_id_loaded_from_flash) {
        memset(joya_app_id, 0, SIZE_APP_ID);
    }

    if (invalid_app_id_setting) {
        err = settings_delete("joya/app_id");
        printk("Deleted legacy/invalid APP_ID setting: %d\n", err);
    }

    printk("Storage init complete: app_id_empty=%d emergency=%d\n",
           is_app_id_empty(), joya_is_in_emergency);

    return 0;
}

bool is_app_id_empty(void) {
    static const uint8_t ceros[SIZE_APP_ID] = {0}; 
    return (memcmp(joya_app_id, ceros, SIZE_APP_ID) == 0);
}

int storage_save_app_id(const uint8_t* new_app_id) {
    int ret;

    memset(joya_app_id, 0, SIZE_APP_ID);
    memcpy(joya_app_id, new_app_id, SIZE_APP_ID);

    ret = settings_save_one("joya/app_id", joya_app_id, SIZE_APP_ID);
    if (ret != 0) {
        printk("Saving APP_ID failed: %d\n", ret);
        memset(joya_app_id, 0, SIZE_APP_ID);
        return ret;
    }

    printk("Saved APP_ID (%d bytes)\n", SIZE_APP_ID);
    return 0;
}

int storage_save_emergency_state(bool is_active)
{
    int ret;
    if(joya_is_in_emergency == is_active) {
        // joya_is_in_emergency has not changed, no need to save to flash
        return 0;
    }

    /*
	 * Note: RAM state is updated first on purpose. Emergency handling is an
	 * operational state and must take effect immediately even if persistence
	 * fails. Flash is only used to restore the state after a reboot.
	 */
    joya_is_in_emergency = is_active;
    ret = settings_save_one("joya/emergency", &joya_is_in_emergency, sizeof(joya_is_in_emergency));
    if(ret != 0) {
        printk("Saving emergency state failed: %d\n", ret);
    }    
    
    return ret;
}

int storage_factory_reset(void)
{
    int app_id_ret;
    int emergency_ret;
    
    app_id_ret = settings_delete("joya/app_id");
    if (app_id_ret != 0 && app_id_ret != -ENOENT) {
        printk("Deleting APP_ID failed: %d\n", app_id_ret);
    }

    emergency_ret = settings_delete("joya/emergency");
    if (emergency_ret != 0 && emergency_ret != -ENOENT) {
        printk("Deleting emergency state failed: %d\n", emergency_ret);
    }

    memset(joya_app_id, 0, sizeof(joya_app_id));
    joya_is_in_emergency = false;
    app_id_loaded_from_flash = false;
    invalid_app_id_setting = false;

    printk("Factory reset storage complete: app_id_delete=%d emergency_delete=%d\n",
           app_id_ret, emergency_ret);

    if (app_id_ret != 0 && app_id_ret != -ENOENT) {
        return app_id_ret;
    }
    if (emergency_ret != 0 && emergency_ret != -ENOENT) {
        return emergency_ret;
    }
    return 0;
}
