#include "app_comm.h"

LOG_MODULE_REGISTER(app_comm, LOG_LEVEL_INF);

char received_app_id[33] = {0}; // Buffer para almacenar el app_id recibido durante el proceso de setup

/**
 * BLE CALLBACKS
 */
void on_ccc_changed_handler(const struct bt_gatt_attr *attr, uint16_t value) {
    return;
}

size_t on_rx_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags){
    if (len != 1) {
        LOG_WRN("Recibida longitud invalida: %d\n", len);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t val = ((uint8_t *)buf)[0];
    LOG_INF("App escribio el comando: 0x%02X\n", val);

    event_type_t ev;
    // Evaluamos el comando de la app (Ej: 0x0A = Detener Emergencia)
    if (val == COMMAND_STOP_EMERGENCY) {
        ev = EV_APP_CMD_STOP_EMERGENCY; // Asegurate que este enum exista en app_events.h
        add_event(ev);
    } else if (val == COMMAND_ACK_EMERGENCY) {
        ev = EV_APP_ACK_EMERGENCY; // Asegurate que este enum exista en app_events.h
        add_event(ev);
    } else if (val == COMMAND_FOLLOW_ME) {
        ev = EV_APP_CMD_FOLLOW_ME; // Asegurate que este enum exista en app_events.h
        add_event(ev);
    } else {
        LOG_WRN("Comando desconocido recibido: 0x%02X\n", val);
    }
    
    return len;
}

ssize_t on_rx_auth_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    // Aca recibis la cadena con el app_id
    if (len > 0 && len <= 32) {
        char temp_id[33];
        memcpy(temp_id, buf, len);
        temp_id[len] = '\0';
        
        // Lo copio a received_app_id para poder compararlo despues en el proceso de autenticacion
        strncpy(received_app_id, temp_id, sizeof(received_app_id) - 1);
        received_app_id[sizeof(received_app_id) - 1] = '\0';
        
        LOG_INF("App ID recibido: %s\n", received_app_id);
        
        // Opcional: Avisar a la FSM que el setup/conexion fue exitoso
        add_event(EV_APP_IDENTIFIER_RECEIVED);
    }
    
    return len;
}

void on_connected_handler(struct bt_conn *conn, uint8_t err) {
    event_type_t ev = EV_BLE_CONNECTED; // Asegurate que este enum exista en app_events.h
    add_event(ev);
}
    
void on_disconnected_handler(struct bt_conn *conn, uint8_t reason) {
    event_type_t ev = EV_BLE_DISCONNECTED; // Asegurate que este enum exista en app_events.h
    add_event(ev);
}

/**
 * @brief Check if the received app_id matches the one stored in flash
 * @param app_id The app_id received during the setup process
 * @return 0 if the app_id matches, non-zero otherwise
 */
int check_app_id(const char *app_id) {
    return strcmp(received_app_id, app_id);
}

void save_received_app_id(void){
    storage_save_app_id(received_app_id);
}