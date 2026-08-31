#ifndef APP_COMM_H
#define APP_COMM_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include "app_state.h"

#define SIZE_APP_ID 16

/**
 * @brief Save the received application identifier.
 * @return 0 on success, non-zero on failure.
 */
int save_received_app_id(void);

/**
 * @brief Compare the saved application identifier with the received identifier.
 * @param saved_app_id Pointer to the saved application identifier to compare.
 * @return 0 if the identifiers match, or a non-zero value otherwise.
 */
int check_app_id(const uint8_t *saved_app_id);

/**
 * @brief Handle CCC (Client Characteristic Configuration) changes.
 * @param attr The GATT attribute.
 * @param value The new CCC value.
 */
void on_ccc_changed_handler(const struct bt_gatt_attr *attr, uint16_t value);

/**
 * @brief Handle writes to the RX characteristic.
 * @param conn The Bluetooth connection.
 * @param attr The GATT attribute.
 * @param buf Buffer containing the received data.
 * @param len Length of the received data.
 * @param offset Offset of the received data.
 * @param flags Write flags.
 * @return The number of bytes processed, or a GATT error.
 */
ssize_t on_rx_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

/**
 * @brief Handle BLE connection events.
 * @param conn The Bluetooth connection.
 * @param err Connection error code.
 */
void on_connected_handler(struct bt_conn *conn, uint8_t err);

/**
 * @brief Handle BLE disconnection events.
 * @param conn The Bluetooth connection.
 * @param reason Disconnection reason code.
 */
void on_disconnected_handler(struct bt_conn *conn, uint8_t reason);

/**
 * @brief Handle writes to the RX authentication characteristic.
 * @param conn The Bluetooth connection.
 * @param attr The GATT attribute.
 * @param buf Buffer containing the received application identifier.
 * @param len Length of the received data.
 * @param offset Offset of the received data.
 * @param flags Write flags.
 * @return The number of bytes processed, or a GATT error.
 */
ssize_t on_rx_auth_write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

/** @brief Application command enumeration. 
*/
enum {
    INIT_JOYA_COMMAND = 0x01,
    COMMAND_ROUTINE = 0x02,
    COMMAND_END_ROUTINE = 0x03,
    COMMAND_EMERGENCY = 0x04,
    COMMAND_ACK = 0x05,
    COMMAND_NACK = 0x06,
    COMMAND_FACTORY_RESET = 0x07,
    END_JOYA_COMMAND = 0x08,

    INIT_APP_COMMAND = 0x40,
    COMMAND_ACK_EMERGENCY = 0x41,
    COMMAND_STOP_EMERGENCY = 0x42,
    COMMAND_FOLLOW_ME = 0x43,
    COMMAND_FRIEND_EMERGENCY = 0x44,
    END_APP_COMMAND = 0x45,
};

#endif