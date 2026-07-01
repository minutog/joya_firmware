#include "app_comm.h"
#include <zephyr/sys/printk.h>

uint8_t received_app_id[SIZE_APP_ID] = {0};
/**
 * BLE CALLBACKS
 */
void on_ccc_changed_handler(const struct bt_gatt_attr *attr, uint16_t value) {
    if(value == BT_GATT_CCC_NOTIFY){
        printk("App event: notifications enabled\n");
        add_event(EV_BLE_NOTIFY_ENABLED);
    } else {
        printk("App event: notifications disabled value=0x%04x\n", value);
    }
    return;
}

ssize_t on_rx_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags){

    // Note: the protocol expects one complete command per BLE write.
    if (offset != 0) {
        printk("Rejecting RX write: invalid offset=%u\n", offset);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len != 1) {
        printk("Rejecting RX write: invalid len=%u\n", len);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t val = ((uint8_t *)buf)[0];
    printk("App command received: 0x%02x\n", val);

    event_type_t ev;
    if (val == COMMAND_STOP_EMERGENCY) {
        ev = EV_APP_STOP_EMERGENCY;
        add_event(ev);
    } else if (val == COMMAND_ACK_EMERGENCY) {
        ev = EV_APP_ACK_EMERGENCY;
        add_event(ev);
    } else if (val == COMMAND_FOLLOW_ME) {
        ev = EV_APP_FOLLOW_ME;
        add_event(ev);
    } else if (val == COMMAND_FRIEND_EMERGENCY) {
        ev = EV_APP_FRIEND_EMERGENCY;
        add_event(ev);
    } else {
        printk("Unknown app command: 0x%02x\n", val);
    }
    
    return len;
}

ssize_t on_rx_auth_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    // Note: APP_ID must be sent in a single BLE write; fragmented writes are not supported.
    if (offset != 0) {
        printk("Rejecting APP_ID: invalid offset=%u\n", offset);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len != SIZE_APP_ID) {
        printk("Rejecting APP_ID: invalid len=%u expected=%d\n", len, SIZE_APP_ID);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    memcpy(received_app_id, buf, SIZE_APP_ID);
    printk("APP_ID received (%d bytes)\n", SIZE_APP_ID);

    add_event(EV_APP_IDENTIFIER_RECEIVED);
    
    return len;
}

void on_connected_handler(struct bt_conn *conn, uint8_t err) {
    printk("Queue event: BLE_CONNECTED\n");
    event_type_t ev = EV_BLE_CONNECTED;
    add_event(ev);
}
    
void on_disconnected_handler(struct bt_conn *conn, uint8_t reason) {
    printk("Queue event: BLE_DISCONNECTED reason=0x%02x\n", reason);
    event_type_t ev = EV_BLE_DISCONNECTED;
    add_event(ev);
}

int check_app_id(const uint8_t *app_id) {
    return memcmp(received_app_id, app_id, SIZE_APP_ID);
}

int save_received_app_id(void){
    int ret = storage_save_app_id(received_app_id);
    memset(received_app_id, 0, SIZE_APP_ID);
    printk("save_received_app_id ret=%d\n", ret);
    return ret;
}
