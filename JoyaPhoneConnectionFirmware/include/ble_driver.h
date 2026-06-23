#ifndef BLE_DRIVER_H
#define BLE_DRIVER_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include "app_state.h"
#include "app_comm.h"

// --- UUIDs of JOYA custom service ---
// Main service UUID
#define BT_UUID_JOYA_SERVICE_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
// TX Characteristic UUID
#define BT_UUID_JOYA_TX_VAL      BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)
// RX Characteristic UUID
#define BT_UUID_JOYA_RX_VAL      BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)
// RX AUTH Characteristic UUID
#define BT_UUID_JOYA_RX_AUTH    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3)

bool is_ble_connected(void);

 /**
  * @brief Initializes the Bluetooth stack and registers the callbacks.
  * @return 0 on success, or a negative error code on failure.
  */
int ble_driver_init(void);

/**
 * @brief Sends a secure event to the mobile app.
 * This function checks if the device is connected before sending the event.
 * @param event_byte The 1-byte code to send.
 * @return 0 on success, or a negative error code on failure.
 */
int ble_send_event_secure(uint8_t event_byte);

/**
 * @brief Starts advertising with the name "Joya Setup".
 * This is used when the device is unclaimed.
 * @return 0 on success, or a negative error code on failure.
 */
int ble_start_setup_advertising(void);

/**
 * @brief Starts advertising with the name "Joya".
 * This is used for reconnection when already paired.
 * @return 0 on success, or a negative error code on failure.
 */
int ble_start_reconnect_advertising(void);

/**
 * @brief Stops any ongoing advertising.
 * @return 0 on success, or a negative error code on failure.
 */
int ble_stop_advertising(void);

/**
 * @brief Forces a reset of the Bluetooth stack.
 * 
 */
void ble_force_reset(void);

bool is_notif_enabled(void);
bool is_authenticated(void);
void set_authenticated(bool auth_status);

#endif // BLE_DRIVER_H