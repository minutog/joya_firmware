#include "flash_memory.h"

uint8_t joya_app_id[SIZE_APP_ID] = {0};
static bool joya_is_in_emergency = false;

static bool storage_persistent_available = false;

bool storage_is_persistent_available(void) {
    return storage_persistent_available;
}

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
    /*
     * Safe RAM defaults.
     * If flash/settings fails, the firmware can still run using RAM only.
     */

    storage_persistent_available = false;

    memset(joya_app_id, 0, SIZE_APP_ID);
    joya_is_in_emergency = false;

    int err = settings_subsys_init();
    if (err) {
        /*
         * Non-critical error: continue with RAM defaults.
         */
        return err;
    }

    err = settings_load();
    if (err) {
        /*
         * Non-critical error: continue with RAM defaults.
         */
        return err;
    }
    
    storage_persistent_available = true;

    return 0;
}

bool is_app_id_empty(void) {
    static const uint8_t zeros[SIZE_APP_ID] = {0}; 
    return (memcmp(joya_app_id, zeros, SIZE_APP_ID) == 0);
}

/*
 * Note: RAM state is updated first on purpose. Flash is only used to restore the 
 * state after a reboot but if it fails, the RAM state is still valid and the 
 * firmware can continue to operate.
 * Returning 1 indicates a non-critical error (flash write failed) but the firmware 
 * can continue to operate with RAM state.
 */

int storage_save_app_id(const uint8_t* new_app_id) {
    int ret;

    memset(joya_app_id, 0, SIZE_APP_ID);
    memcpy(joya_app_id, new_app_id, SIZE_APP_ID);

    if (!storage_persistent_available) {
        return 1; // Return an error code indicating that flash is not available
    }

    ret = settings_save_one("joya/app_id", joya_app_id, SIZE_APP_ID);
    if (ret != 0) {
        /*
         * Non-critical error: continue with RAM defaults.
         */
        return 1;
    }

    return 0;
}

int storage_save_emergency_state(bool is_active)
{
    joya_is_in_emergency = is_active;

    if (!storage_persistent_available) {
        return 1; // Return an error code indicating that flash is not available
    }

    int ret = settings_save_one("joya/emergency", &joya_is_in_emergency, sizeof(joya_is_in_emergency));
    if (ret != 0) {
        /*
         * Non-critical error: continue with RAM defaults.
         */
        return 1;
    }
    
    return 0;
}

/**
 * Even if the flash write fails, we still clear the RAM state 
 * to ensure that the device does not operate with stale data.
 */
int storage_factory_reset(void)
{    
    memset(joya_app_id, 0, sizeof(joya_app_id));
    joya_is_in_emergency = false;

    if(!storage_persistent_available) {
        return 1; // Return an error code indicating that flash is not available
    }

    int ret = settings_delete("joya/app_id");
    if (ret != 0) {
        return 1;
    }

    ret = settings_delete("joya/emergency");
    if (ret != 0) {
        return 1;
    }

    return 0;
}