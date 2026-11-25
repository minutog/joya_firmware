#include "flash_memory.h"
#include "diagnostics.h"

uint8_t joya_app_id[SIZE_APP_ID] = {0};
static emergency_phase_t joya_emergency_phase = EMERGENCY_NONE;
static uint8_t joya_pending_routine;
static uint8_t pending_routine_owner[SIZE_APP_ID];
static bool pending_routine_dirty;

static bool storage_persistent_available = false;

static char joya_name[JOYA_NAME_MAX_LEN + 1] = JOYA_ADV_NAME_DEFAULT;


const char *storage_get_name(void){
    return joya_name;
}

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
        if (len != sizeof(uint8_t)) {
            return -EINVAL;
        }
        uint8_t stored;
        rc = read_cb(cb_arg, &stored, len);
        if (rc < 0) {
            return rc;
        }

        if (rc != len) {
            return -EINVAL;
        }

        return emergency_phase_decode(stored, &joya_emergency_phase) ? 0 : -EINVAL;
    }

    if (settings_name_steq(name, "routine", &next) && !next) {
        uint8_t stored[1 + SIZE_APP_ID];
        if (len != 1 && len != sizeof(stored)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, stored, len);
        if (rc != len ||
            (stored[0] != 0 && stored[0] != COMMAND_ROUTINE &&
             stored[0] != COMMAND_END_ROUTINE) ||
            (stored[0] != 0 && len != sizeof(stored))) {
            return -EINVAL;
        }
        joya_pending_routine = stored[0];
        memset(pending_routine_owner, 0, sizeof(pending_routine_owner));
        if (joya_pending_routine != 0) {
            memcpy(pending_routine_owner, stored + 1, SIZE_APP_ID);
        }
        return 0;
    }

    // Evaluate DEVICE NAME
    if (settings_name_steq(name, "name", &next) && !next) {

        if (len == 0 || len > JOYA_NAME_MAX_LEN + 1) {
            return -EINVAL;
        }

        char temp[JOYA_NAME_MAX_LEN + 1];

        rc = read_cb(cb_arg, temp, len);
        if (rc < 0) {
            return rc;
        }

        if (rc != len || temp[len - 1] != '\0') {
            return -EINVAL;
        }

        if (strlen(temp) != len - 1) {
            return -EINVAL;
        }

        memcpy(joya_name, temp, len);

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
    return joya_emergency_phase != EMERGENCY_NONE;
}

emergency_phase_t storage_get_emergency_phase(void) {
    return joya_emergency_phase;
}

uint8_t storage_get_pending_routine(void)
{
    return joya_pending_routine;
}

int storage_save_pending_routine(uint8_t command)
{
    if (command != 0 && command != COMMAND_ROUTINE &&
        command != COMMAND_END_ROUTINE) {
        return -EINVAL;
    }
    if (command != 0 && is_app_id_empty()) {
        return -EACCES;
    }
    if (command != 0 && is_in_emergency()) {
        return -EBUSY;
    }
    if (command == joya_pending_routine && !pending_routine_dirty) {
        return 0;
    }

    joya_pending_routine = command;
    if (command == 0) {
        memset(pending_routine_owner, 0, sizeof(pending_routine_owner));
    } else {
        memcpy(pending_routine_owner, joya_app_id, SIZE_APP_ID);
    }
    if (!storage_persistent_available) {
        pending_routine_dirty = true;
        return 1;
    }
    uint8_t record[1 + SIZE_APP_ID];
    record[0] = command;
    if (command != 0) {
        memcpy(record + 1, joya_app_id, SIZE_APP_ID);
    }
    int ret = settings_save_one("joya/routine", record,
                                command == 0 ? 1 : sizeof(record));
    pending_routine_dirty = ret != 0;
    if (ret != 0) {
        diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
        return 1;
    }
    return 0;
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
    joya_emergency_phase = EMERGENCY_NONE;
    joya_pending_routine = 0;
    memset(pending_routine_owner, 0, sizeof(pending_routine_owner));
    pending_routine_dirty = false;
    strcpy(joya_name, JOYA_ADV_NAME_DEFAULT);

    int err = settings_subsys_init();
    if (err) {
        /*
         * Non-critical error: continue with RAM defaults.
         */
        return err;
    }

    err = settings_load();
    if (err) {
        diagnostics_report_error(DIAG_ERROR_SETTINGS_LOAD);
        /*
         * Settings may have been partially loaded.
         * Restore safe RAM defaults.
         */
        memset(joya_app_id, 0, SIZE_APP_ID);
        joya_emergency_phase = EMERGENCY_NONE;
        joya_pending_routine = 0;
        memset(pending_routine_owner, 0, sizeof(pending_routine_owner));
        pending_routine_dirty = false;
        strcpy(joya_name, JOYA_ADV_NAME_DEFAULT);

        return err;
    }

    storage_persistent_available = true;

    /* An emergency or a missing owner APP_ID suppresses any older routine. */
    if (joya_pending_routine != 0 &&
        (is_app_id_empty() || joya_emergency_phase != EMERGENCY_NONE ||
         memcmp(pending_routine_owner, joya_app_id, SIZE_APP_ID) != 0)) {
        (void)storage_save_pending_routine(0);
    }

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

    /* If cleanup after reset failed, do not bind an old routine to a new app. */
    if (is_app_id_empty() && pending_routine_dirty &&
        storage_save_pending_routine(0) != 0) {
        return 1;
    }

    memset(joya_app_id, 0, SIZE_APP_ID);
    memcpy(joya_app_id, new_app_id, SIZE_APP_ID);

    if (!storage_persistent_available) {
        return 1; // Return an error code indicating that flash is not available
    }

    ret = settings_save_one("joya/app_id", joya_app_id, SIZE_APP_ID);
    if (ret != 0) {
        diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
        /*
         * Non-critical error: continue with RAM defaults.
         */
        return 1;
    }

    return 0;
}

int storage_save_emergency_phase(emergency_phase_t phase)
{
    if (phase != EMERGENCY_NONE && phase != EMERGENCY_PENDING_ACK &&
        phase != EMERGENCY_ACKED) {
        return -EINVAL;
    }
    /* Do not make a stale routine visible after STOP if its tombstone failed. */
    if (phase == EMERGENCY_NONE && pending_routine_dirty &&
        storage_save_pending_routine(0) != 0) {
        joya_emergency_phase = phase;
        return 1;
    }
    joya_emergency_phase = phase;

    if (!storage_persistent_available) {
        return 1; // Return an error code indicating that flash is not available
    }

    uint8_t stored = (uint8_t)phase;
    int ret = settings_save_one("joya/emergency", &stored, sizeof(stored));
    if (ret != 0) {
        diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
        /*
         * Non-critical error: continue with RAM defaults.
         */
        return 1;
    }
    
    return 0;
}

int storage_save_name(const char *name)
{
    if (name == NULL) {
        return -EINVAL;
    }

    size_t len = strnlen(name, JOYA_NAME_MAX_LEN + 1);

    if (len == 0 || len > JOYA_NAME_MAX_LEN) {
        return -EINVAL;
    }

    // Update RAM first
    memcpy(joya_name, name, len + 1);

    if (!storage_persistent_available) {
        return 1;
    }

    int ret = settings_save_one("joya/name",
                                joya_name,
                                len + 1);

    if (ret != 0) {
        diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
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
    /*
     * Clear RAM state first, regardless of Flash availability.
     */
    memset(joya_app_id, 0, sizeof(joya_app_id));
    joya_emergency_phase = EMERGENCY_NONE;
    joya_pending_routine = 0;
    memset(pending_routine_owner, 0, sizeof(pending_routine_owner));
    strcpy(joya_name, JOYA_ADV_NAME_DEFAULT);

    if (!storage_persistent_available) {
        return 1;
    }

    /*
     * Attempt to delete all settings, even if one operation fails.
     */
    int result = 0;

    /* Delete the command before its APP_ID owner. */
    if (settings_delete("joya/routine") != 0) {
        pending_routine_dirty = true;
        result = 1;
    } else {
        pending_routine_dirty = false;
    }

    if (settings_delete("joya/app_id") != 0) {
        result = 1;
    }

    if (settings_delete("joya/emergency") != 0) {
        result = 1;
    }

    if (settings_delete("joya/name") != 0) {
        result = 1;
    }

    return result;
}
