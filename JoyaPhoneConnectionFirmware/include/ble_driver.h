#ifndef BLE_DRIVER_H
#define BLE_DRIVER_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include "app_state.h"
#include "app_comm.h"

// --- UUIDs of JOYA custom service ---
// Main service UUID

// JOYA COMMANDS SERVICE
#define BT_UUID_JOYA_COMMANDS_SERVICE_VAL       BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585000)
#define BT_UUID_JOYA_TX_VAL                     BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585001)
#define BT_UUID_JOYA_RX_VAL                     BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585002)

// JOYA AUTHENTICATION SERVICE
#define BT_UUID_JOYA_AUTH_SERVICE_VAL           BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585100)
#define BT_UUID_JOYA_RX_AUTH_VAL                BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585101)

// JOYA CONFIGURATION SERVICE
#define BT_UUID_JOYA_CONFIG_SERVICE_VAL         BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585200)
#define BT_UUID_JOYA_NAME_VAL                   BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585201)

// JOYA BATTERY SERVICE: change = 0/1/2; measurement = battery voltage in mV
#define BT_UUID_JOYA_BATTERY_SERVICE_VAL        BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585300)
#define BT_UUID_JOYA_BATTERY_CHANGE_VAL         BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585301)
#define BT_UUID_JOYA_BATTERY_MEASUREMENT_VAL    BT_UUID_128_ENCODE(0xa407e00a, 0x00c1, 0x464d, 0x9173, 0x2cb8be585302)

/**
 * @brief Initialize the Bluetooth stack.
 * @return 0 on success, or a negative error code on failure.
 */
int ble_driver_init(void);
bool ble_is_current_conn(struct bt_conn *conn);
bool ble_is_authenticated_conn(struct bt_conn *conn);
bool ble_mark_authenticated_conn(struct bt_conn *conn);

/**
 * @brief Send a secure event to the mobile app.
 * This function checks if the device is connected before sending the event.
 * @param event_byte The 1-byte code to send.
 * @return 0 on success, or a negative error code on failure.
 */
 /**
 * Note: only checks if the connection is active and notifications are enabled. It does not check if the app is authenticated. This responsability is delegated to the caller.
 */
int ble_send_event_secure(uint8_t event_byte);

/** Notify the confirmed battery level on the change characteristic. */
int ble_send_battery_change(uint8_t level);

/** Notify battery voltage in mV as an unsigned 16-bit little-endian value. */
int ble_send_battery_measurement(int32_t voltage_mv);

/**
 * @brief Start advertising for a new connection or reconnection.
 * @param is_new_connection Set to true for new connections (JOYA SETUP), false for reconnections (JOYA).
 * @return 0 on success, or a negative error code on failure.
 */
int ble_start_setup_advertising(bool is_new_connection);


/**
 * @brief Stop any ongoing advertising.
 * @return 0 on success, or a negative error code on failure.
 */
int ble_stop_advertising(void);

/**
 * @brief Force a Bluetooth disconnection and stop advertising.
 * @return 0 on success, or a negative error code on failure.
 */
int ble_disconnect(void);

int ble_start_setup_advertising_backup(void);

#endif // BLE_DRIVER_H
