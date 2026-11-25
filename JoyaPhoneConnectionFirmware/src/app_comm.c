#include "app_comm.h"

/**
 * BLE CALLBACKS
 */
ssize_t on_rx_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags){

    // Note: the protocol expects one complete command per BLE write.
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t val = ((uint8_t *)buf)[0];

    event_type_t ev;
    if (val == COMMAND_STOP_EMERGENCY) {
        ev = EV_APP_STOP_EMERGENCY;
    } else if (val == COMMAND_ACK_EMERGENCY) {
        ev = EV_APP_ACK_EMERGENCY;
    } else if (val == COMMAND_FOLLOW_ME) {
        ev = EV_APP_FOLLOW_ME;
    } else if (val == COMMAND_FRIEND_EMERGENCY) {
        ev = EV_APP_FRIEND_EMERGENCY;
    } else if (val == COMMAND_OTA_ENABLE) {
        ev = EV_APP_OTA_ENABLE;
    } else if (val == COMMAND_OTA_DISABLE) {
        ev = EV_APP_OTA_DISABLE;
    } else {
        return len;
    }
    if (add_event_for_conn(ev, conn, NULL) != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }
    
    return len;
}

ssize_t on_rx_auth_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    // Note: APP_ID must be sent in a single BLE write; fragmented writes are not supported.
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len != SIZE_APP_ID) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    if (add_event_for_conn(EV_APP_IDENTIFIER_RECEIVED, conn, buf) != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }
    
    return len;
}

void on_connected_handler(struct bt_conn *conn, uint8_t err) {
    event_type_t ev = EV_BLE_CONNECTED;
    add_event_for_conn(ev, conn, NULL);
}
    
void on_disconnected_handler(struct bt_conn *conn, uint8_t reason) {
    event_type_t ev = EV_BLE_DISCONNECTED;
    add_event_for_conn(ev, conn, NULL);
}

int check_app_id(const uint8_t *received_app_id, const uint8_t *app_id) {
    return memcmp(received_app_id, app_id, SIZE_APP_ID);
}

int save_received_app_id(const uint8_t *received_app_id){
    int ret = storage_save_app_id(received_app_id);
    if (ret != 0) {
        // LOG: Failed to save received APP_ID - continuing without saving
    }
    return ret;
}
