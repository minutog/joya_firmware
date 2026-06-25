#include "flash_memory.h"

LOG_MODULE_REGISTER(app_flash, LOG_LEVEL_INF);

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
            LOG_ERR("Error: Tamaño en Flash (%d) no coincide con SIZE_APP_ID", len);
            return -EINVAL; 
        }

        rc = read_cb(cb_arg, joya_app_id, len);
        if (rc < 0) {
            LOG_ERR("Error leyendo app_id desde Flash: %d", rc);
            return rc;
        }

        app_id_loaded_from_flash = true;
        LOG_INF("app_id cargado desde Flash con exito.");
        LOG_HEXDUMP_DBG(joya_app_id, SIZE_APP_ID, "Contenido de app_id:"); 
        
        return rc;
    }

    // Evaluate EMERGENCY
    if (settings_name_steq(name, "emergency", &next) && !next) {
        if (len != sizeof(joya_is_in_emergency)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &joya_is_in_emergency, len);
        LOG_INF("Cargado desde Flash -> emergency: %d", joya_is_in_emergency);
        return rc;
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
        LOG_ERR("Error inicializando Settings (err %d)", err);
        return err;
    }

    LOG_INF("Cargando configuraciones desde Flash...");
    // Esto dispara la lectura en Flash y llama a tu función 'app_settings_set'
    err = settings_load();
    if (err) {
        LOG_ERR("Error cargando Settings desde Flash: %d", err);
        return err;
    }
    
    if (!app_id_loaded_from_flash) {
        LOG_INF("No app_id in flash. Using empty app_id in RAM.");
        memset(joya_app_id, 0, SIZE_APP_ID);
    } else {
        LOG_INF("El ID ya existía en la Flash. Saltando escritura para proteger el silicio.");
    }

    LOG_INF("Subsistema de almacenamiento listo.");
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
        LOG_ERR("Error al guardar app_id en Flash. Codigo: %d", ret);
        memset(joya_app_id, 0, SIZE_APP_ID);
        return ret; // Propagamos el error de la Flash
    }

    LOG_INF("app_id guardado de forma segura en Flash");
    return 0; // Éxito total
}

int storage_save_emergency_state(bool is_active)
{
    int ret;
    if(joya_is_in_emergency == is_active) {
        LOG_INF("Estado de emergencia ya es %d, no se necesita guardar en Flash", is_active);
        return 0;
    }

    /*
	 * Note: RAM state is updated first on purpose. Emergency handling is an
	 * operational state and must take effect immediately even if persistence
	 * fails. Flash is only used to restore the state after a reboot.
	 */
    joya_is_in_emergency = is_active;
    ret = settings_save_one("joya/emergency", &joya_is_in_emergency, sizeof(joya_is_in_emergency));
    // (to do): decide what to do if settings_save_one fails (e.g., retry, log error, etc.)
    
    LOG_INF("Estado de emergencia guardado en Flash");
    return ret;
}

int storage_factory_reset(void)
{
    int ret;
    
    ret = settings_delete("joya/app_id");
    if (ret != 0) {
        LOG_ERR("Error al borrar app_id de Flash (err %d)", ret);
        return ret;
    }

    ret = settings_delete("joya/emergency");
    if (ret != 0) {
        LOG_ERR("Error al borrar estado de emergencia de Flash (err %d)", ret);
        return ret;
    }

    memset(joya_app_id, 0, sizeof(joya_app_id));
    joya_is_in_emergency = false;

    LOG_INF("Memoria Flash borrada (Factory Reset)");
    return 0;
}