#include "ble_driver.h"

#define JOYA_ADV_NAME_SETUP      "Joya Setup"
#define JOYA_ADV_NAME_RECONNECT  "Joya"

/** @brief For logging */
LOG_MODULE_REGISTER(ble_driver, LOG_LEVEL_INF);

/** @brief UUIDs for the GATT service */
static struct bt_uuid_128 joya_svc_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_SERVICE_VAL);
static struct bt_uuid_128 joya_tx_uuid  = BT_UUID_INIT_128(BT_UUID_JOYA_TX_VAL);
static struct bt_uuid_128 joya_rx_uuid  = BT_UUID_INIT_128(BT_UUID_JOYA_RX_VAL);
static struct bt_uuid_128 joya_rx_auth_uuid  = BT_UUID_INIT_128(BT_UUID_JOYA_RX_AUTH_VAL);

static const struct bt_le_adv_param joya_adv_param = {
	.options = BT_LE_ADV_OPT_CONNECTABLE |
		   BT_LE_ADV_OPT_ONE_TIME |
		   BT_LE_ADV_OPT_USE_NAME,
	.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
	.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
	.peer = NULL,
};

/** @brief Current connection */
static struct bt_conn *current_conn = NULL;
static bool notify_enabled = false;
static bool authenticated = false;

/**
 * CALLBACKS
 */

/**
 * @brief Callback for when the CCCD (Client Characteristic Configuration Descriptor) changes.
 * This is called when the client (phone) enables or disables notifications for the TX characteristic.
 * @param attr The GATT attribute that changed (the CCCD).
 * @param value The new value of the CCCD
 * 
 */

static void on_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Notifications %s", notify_enabled ? "enabled" : "disabled");
    on_ccc_changed_handler(attr, value);
}

/** @brief Callback for when the RX characteristic is written to.
 * This is called when the client (phone) writes data to the RX characteristic.
 * @param conn The Bluetooth connection.
 * @param attr The GATT attribute that was written to.
 * @param buf The buffer containing the data written.
 * @param len The length of the data written.
 * @param offset The offset of the data written.
 * @param flags The flags for the write operation.
 * @return The number of bytes written or an error code.
 */
static ssize_t on_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    return on_rx_write_handler(conn, attr, buf, len, offset, flags);
}

static ssize_t on_rx_auth_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    return on_rx_auth_write_handler(conn, attr, buf, len, offset, flags);
}

/** @brief Callback for when a Bluetooth connection is established.
 * This is called when the client (phone) connects to the device.
 * @param conn The Bluetooth connection.
 * @param err The error code (0 if no error).
 */
static void on_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Connection error: %d", err);
        return;
    }
    current_conn = bt_conn_ref(conn);
    LOG_INF("Phone connected");

    on_connected_handler(conn, err);
}

/** @brief Callback for when a Bluetooth connection is lost.
 * This is called when the client (phone) disconnects from the device.
 * @param conn The Bluetooth connection.
 * @param reason The reason for the disconnection.
 */
static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    LOG_INF("Phone disconnected (Reason: %d)", reason);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    notify_enabled = false;

    on_disconnected_handler(conn, reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

/**
 * GATT SERVICE DEFINITION
 */

/** @brief Defines the GATT service for the Joya device.
 */
BT_GATT_SERVICE_DEFINE(joya_svc,
    BT_GATT_PRIMARY_SERVICE(&joya_svc_uuid),
    
    // TX Characteristic UUID: supports Notifications
    BT_GATT_CHARACTERISTIC(&joya_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),

    BT_GATT_CCC(on_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    
    // RX Characteristic UUID: supports Write and Write Without Response
    BT_GATT_CHARACTERISTIC(&joya_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_rx_write, NULL),

    // RX AUTH Characteristic UUID: supports Write and Write Without Response (to do: maybe add some security requirements)
    BT_GATT_CHARACTERISTIC(&joya_rx_auth_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_rx_auth_write, NULL)
);

/** @brief Defines the advertising data for the Joya device.
 */
static const struct bt_data ad[] = {
    // (to do): review
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

/**
 * @brief Sends a notification to the mobile app (do not use directly, use ble_send_event_secure instead).
 * @param event_byte The 1-byte code to send.
 * @return 0 on success, or a negative error code on failure.
 */
static int ble_send_notify(uint8_t event_byte) {
    if (!current_conn || !notify_enabled) {
        LOG_WRN("Cannot send notification (No connection or CCCD disabled)");
        return -ENOTCONN;
    }

    const struct bt_gatt_attr *attr = bt_gatt_find_by_uuid(joya_svc.attrs, 0xFFFF, &joya_tx_uuid.uuid);
    if (!attr) {
        return -EINVAL;
    }

    int err = bt_gatt_notify(current_conn, attr, &event_byte, sizeof(event_byte));
    LOG_INF("Notification sent: 0x%02X (Status: %d)", event_byte, err);
    return err;
}

/** 
 * PUBLIC API
 */


int ble_driver_init(void) {
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Error enabling BT: %d", err);
        return err;
    }
    
    LOG_INF("Bluetooth initialized successfully");
    return 0;
}

/*
int ble_start_setup_advertising(void) {
    // Change the device name to "Joya Setup" for advertising
    int name_err = bt_set_name("Joya Setup");
    if (name_err) {
        LOG_WRN("Could not set dynamic name: %d", name_err);
    }

    int err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Failed to start setup advertising. Error: %d", err);
        return err;
    }

    LOG_INF("Setup advertising started successfully");
    return 0;
}*/

int ble_start_setup_advertising(void)
{
	int err;

	err = bt_le_adv_stop();
	if (err < 0 && err != -EALREADY) {
		LOG_WRN("Failed to stop advertising before setup adv: %d", err);
	}

	err = bt_set_name(JOYA_ADV_NAME_SETUP);
	if (err < 0) {
		LOG_ERR("Failed to set setup BLE name: %d", err);
		return err;
	}

	err = bt_le_adv_start(&joya_adv_param, NULL, 0, NULL, 0);
	if (err < 0) {
		LOG_ERR("Failed to start setup advertising: %d", err);
		return err;
	}

	LOG_INF("Setup advertising started successfully");

	return 0;
}

/*
int ble_start_reconnect_advertising(void) {
    bt_set_name("Joya");
    int err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    LOG_INF("Setup advertising started successfully");
    return err;
}*/
int ble_start_reconnect_advertising(void)
{
	int err;

	err = bt_le_adv_stop();
	if (err < 0 && err != -EALREADY) {
		LOG_WRN("Failed to stop advertising before reconnect adv: %d", err);
	}

	err = bt_set_name(JOYA_ADV_NAME_RECONNECT);
	if (err < 0) {
		LOG_ERR("Failed to set reconnect BLE name: %d", err);
		return err;
	}

	err = bt_le_adv_start(&joya_adv_param, NULL, 0, NULL, 0);
	if (err < 0) {
		LOG_ERR("Failed to start reconnect advertising: %d", err);
		return err;
	}

	LOG_INF("Reconnect advertising started successfully");

	return 0;
}


int ble_stop_advertising(void) {
    return bt_le_adv_stop();
}


int ble_send_event_secure(uint8_t event_byte) {
    if (current_conn == NULL) {
        LOG_WRN("BLE disconnected");
        // Optional: reconnecting (to do)
        return -ENOTCONN;
    }
    return ble_send_notify(event_byte);
}

bool is_ble_connected(void) {
    return current_conn != NULL;
}

void ble_force_reset(void) {
    if (current_conn) {
        int err = bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (err) {
            LOG_ERR("Failed to disconnect: %d", err);
        } else {
            LOG_INF("Disconnection initiated...");
        }

        // Note: the stack will automatically trigger the on_disconnected callback 
        // when the disconnection is complete, so that's where the 'current_conn' 
        // pointer is actually cleaned up to NULL.
    }

    // Stop any advertising that might be running
    bt_le_adv_stop();
    LOG_INF("Radio silenced.");
}

void set_authenticated(bool auth) {
    authenticated = auth;
}

bool is_authenticated(void) {
    return authenticated;
}

bool is_notify_enabled(void) {
    return notify_enabled;
}