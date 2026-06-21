#include "app_comm.h"

LOG_MODULE_REGISTER(app_comm, LOG_LEVEL_INF);

/**
 * BLE CALLBACKS
 */
void on_ccc_changed_handler(const struct bt_gatt_attr *attr, uint16_t value) {
    return;
}

size_t on_rx_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags){
    if (len != 1) {
        LOG_WRN("Recibida longitud invalida: %d", len);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t val = ((uint8_t *)buf)[0];
    LOG_INF("App escribio el comando: 0x%02X", val);

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
        LOG_WRN("Comando desconocido recibido: 0x%02X", val);
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