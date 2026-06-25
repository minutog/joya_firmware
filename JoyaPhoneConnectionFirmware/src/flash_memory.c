#include "flash_memory.h"

bool app_id_loaded_from_flash = false;
uint8_t joya_app_id[SIZE_APP_ID] = {0};
static bool joya_is_in_emergency = false;

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
            return -EINVAL; 
        }

        rc = read_cb(cb_arg, joya_app_id, len);
        if (rc < 0) {
            return rc;
        }

        if (rc != len) {
            return -EINVAL;
        }

        app_id_loaded_from_flash = true;

        return 0;
    }

    // Evaluate EMERGENCY
    if (settings_name_steq(name, "emergency", &next) && !next) {
        if (len != sizeof(joya_is_in_emergency)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &joya_is_in_emergency, len);
        if (rc < 0) {
            return rc;
        }

        if (rc != len) {
            return -EINVAL;
        }

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
        return err;
    }

    // Esto dispara la lectura en Flash y llama a tu función 'app_settings_set'
    err = settings_load();
    if (err) {
        return err;
    }
    
    if (!app_id_loaded_from_flash) {
        memset(joya_app_id, 0, SIZE_APP_ID);
    }

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
        memset(joya_app_id, 0, SIZE_APP_ID);
        return ret;
    }

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
        // (improvement): decide what to do if settings_save_one fails (e.g., retry, log error, etc.)
    }    
    
    return ret;
}

int storage_factory_reset(void)
{
    int ret;
    
    ret = settings_delete("joya/app_id");
    if (ret != 0) {
        return ret;
    }

    ret = settings_delete("joya/emergency");
    if (ret != 0) {
        return ret;
    }

    memset(joya_app_id, 0, sizeof(joya_app_id));
    joya_is_in_emergency = false;

    return 0;
}