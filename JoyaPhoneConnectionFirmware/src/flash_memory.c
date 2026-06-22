#include "flash_memory.h"

LOG_MODULE_REGISTER(app_flash, LOG_LEVEL_INF);

char joya_app_id[33] = {0}; // 32 char + null terminator
static bool joya_is_in_emergency = false; // Variable to store the emergency state

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

    // Evaluate if the key being loaded is "joya/app_id" (and checks that there are no subkeys after "app_id")
    if (settings_name_steq(name, "app_id", &next) && !next) {
        // Validate the length of the data being loaded to prevent buffer overflow
        if (len > sizeof(joya_app_id) - 1) {
            return -EINVAL; // El dato en flash es muy grande
        }
        rc = read_cb(cb_arg, joya_app_id, len);
        joya_app_id[len] = '\0'; // Aseguramos el fin de string
        LOG_INF("Cargado desde Flash -> app_id: %s", joya_app_id);
        return rc;
    }

    if (settings_name_steq(name, "emergency", &next) && !next) {
        if (len != sizeof(joya_is_in_emergency)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &joya_is_in_emergency, len);
        LOG_INF("Cargado desde Flash -> emergency: %d", joya_is_in_emergency);
        return rc;
    }

    return -ENOENT; // Clave no reconocida
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
//SETTINGS_STATIC_SUBNODE_DEFINE(joya_settings, "joya", NULL, app_settings_set, NULL, NULL);
SETTINGS_STATIC_HANDLER_DEFINE(joya, "joya", NULL, app_settings_set, NULL, NULL);

/**
 * PUBLIC API
 */

/**
 * @brief Initialize the storage subsystem and load settings from flash
 * This function should be called at the start of the application to ensure that any previously saved settings are loaded into RAM and ready for use.
 */
void storage_init(void)
{
    // Inicializar el subsistema
    int err = settings_subsys_init();
    if (err) {
        LOG_ERR("Error inicializando Settings (err %d)", err);
        return;
    }

    LOG_INF("Cargando configuraciones desde Flash...");
    // Esto dispara la lectura en Flash y llama a tu función 'app_settings_set'
    settings_load(); 
}

/**
 * @brief Save the app_id to flash
 * @param new_app_id The new app_id to save (must be a null-terminated string with a maximum length of 32 characters)
 * 
 */
void storage_save_app_id(const char* new_app_id)
{
    // 1. Lo guardás en tu variable de RAM
    strncpy(joya_app_id, new_app_id, sizeof(joya_app_id) - 1);
    
    // 2. Lo grabás físicamente en la Flash con la clave "joya/app_id"
    settings_save_one("joya/app_id", joya_app_id, strlen(joya_app_id));
    LOG_INF("app_id guardado en Flash");
}

/**
 * @brief Save the emergency state to flash
 * @param is_active The new emergency state to save (true for active, false for inactive)
 */
void storage_save_emergency_state(bool is_active)
{
    joya_is_in_emergency = is_active;
    settings_save_one("joya/emergency", &joya_is_in_emergency, sizeof(joya_is_in_emergency));
    LOG_INF("Estado de emergencia guardado en Flash");
}

/**
 * @brief Factory reset: Clear all relevant settings from flash
 */
void storage_factory_reset(void)
{
    settings_delete("joya/app_id");
    settings_delete("joya/emergency");
    memset(joya_app_id, 0, sizeof(joya_app_id));
    joya_is_in_emergency = false;
    LOG_INF("Memoria Flash borrada (Factory Reset)");
}