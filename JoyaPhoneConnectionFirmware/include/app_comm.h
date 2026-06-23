#ifndef AAP_COMM_H
#define AAP_COMM_H

#include <stdint.h>
#include "app_state.h"
#include "flash_memory.h"

#define SIZE_APP_ID 5

int save_received_app_id(void);
int check_app_id(const uint8_t *app_id);

/** @brief Handler for CCC (Client Characteristic Configuration) changed events
 * @param attr The GATT attribute
 * @param value The new value
 */
void on_ccc_changed_handler(const struct bt_gatt_attr *attr, uint16_t value);

/** @brief Handler for RX (Receive) write events
 * @param conn The Bluetooth connection
 * @param attr The GATT attribute
 * @param buf The buffer containing the received data
 * @param len The length of the received data
 * @param offset The offset of the received data
 * @param flags The flags for the received data
 * @return The number of bytes processed
 */
size_t on_rx_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

/** @brief Handler for connected events
 * @param conn The Bluetooth connection
 * @param err The error code
 */
void on_connected_handler(struct bt_conn *conn, uint8_t err);

/** @brief Handler for disconnected events
 * @param conn The Bluetooth connection
 * @param reason The reason for disconnection
 */
void on_disconnected_handler(struct bt_conn *conn, uint8_t reason);

ssize_t on_rx_auth_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags);


enum {
    INIT_JOYA_COMMAND = 0x01,
    COMMAND_ROUTINE = 0x02,
    COMMAND_END_ROUTINE = 0x03,
    COMMAND_EMERGENCY = 0x04,
    COMMAND_ACK_AUTH = 0x05,
    COMMAND_NACK_AUTH = 0x06,
    END_JOYA_COMMAND = 0x07,

    INIT_APP_COMMAND = 0x40,
    COMMAND_STOP_EMERGENCY = 0x41,
    COMMAND_ACK_EMERGENCY = 0x42,
    COMMAND_FOLLOW_ME = 0x43,
    END_APP_COMMAND = 0x44,
};

#endif