#include "ble_driver.h"
#include "flash_memory.h"
#include "ota_access.h"
#include <string.h>

#define JOYA_ADV_NAME_SETUP     "Joya Setup"

#define JOYA_SUFFIX_MAX_LEN     (JOYA_NAME_MAX_LEN - (sizeof(JOYA_NAME_PREFIX) - 1))


/** @brief UUIDs for the GATT services and characteristics */
static struct bt_uuid_128 joya_commands_svc_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_COMMANDS_SERVICE_VAL);

static struct bt_uuid_128 joya_tx_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_TX_VAL);

static struct bt_uuid_128 joya_rx_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_RX_VAL);

static struct bt_uuid_128 joya_auth_svc_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_AUTH_SERVICE_VAL);

static struct bt_uuid_128 joya_rx_auth_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_RX_AUTH_VAL);

static struct bt_uuid_128 joya_config_svc_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_CONFIG_SERVICE_VAL);

static struct bt_uuid_128 joya_name_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_NAME_VAL);

static struct bt_uuid_128 joya_battery_svc_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_BATTERY_SERVICE_VAL);
static struct bt_uuid_128 joya_battery_change_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_BATTERY_CHANGE_VAL);
static struct bt_uuid_128 joya_battery_measurement_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_BATTERY_MEASUREMENT_VAL);


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
static bool current_conn_authenticated = false;
static bool notify_enabled = false;
static bool battery_change_notify_enabled = false;
static bool battery_measurement_notify_enabled = false;

static K_MUTEX_DEFINE(conn_mutex);

/**
 * CALLBACKS (see app_comm.c)
 */

static void on_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    bool enabled = (value == BT_GATT_CCC_NOTIFY);

    k_mutex_lock(&conn_mutex, K_FOREVER);
    notify_enabled = enabled;
    k_mutex_unlock(&conn_mutex);
        
    on_ccc_changed_handler(attr, value);
}

static void on_battery_change_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    k_mutex_lock(&conn_mutex, K_FOREVER);
    battery_change_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    k_mutex_unlock(&conn_mutex);
}

static void on_battery_measurement_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    k_mutex_lock(&conn_mutex, K_FOREVER);
    battery_measurement_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    k_mutex_unlock(&conn_mutex);
}

static ssize_t on_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
                            
    return on_rx_write_handler(conn, attr, buf, len, offset, flags);
}

static ssize_t on_rx_auth_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    return on_rx_auth_write_handler(conn, attr, buf, len, offset, flags);
}


static void on_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        // (improvement): decide what to do if connection fails (e.g., retry, log error, etc.)
        return;
    }

    k_mutex_lock(&conn_mutex, K_FOREVER);
    if(current_conn){
        k_mutex_unlock(&conn_mutex);
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return;
    }

    current_conn = bt_conn_ref(conn);
    current_conn_authenticated = false;
    notify_enabled = false;
    battery_change_notify_enabled = false;
    battery_measurement_notify_enabled = false;

    k_mutex_unlock(&conn_mutex);


    on_connected_handler(conn, err);
}


static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    struct bt_conn *old_conn = NULL;
    k_mutex_lock(&conn_mutex, K_FOREVER);

    if(current_conn == conn) {
        old_conn = current_conn;
        current_conn = NULL;
        current_conn_authenticated = false;
    }

    notify_enabled = false;
    battery_change_notify_enabled = false;
    battery_measurement_notify_enabled = false;
    k_mutex_unlock(&conn_mutex);

    if(old_conn) {
        ota_access_on_disconnect();
        bt_conn_unref(old_conn);
    }


    on_disconnected_handler(conn, reason);
}

static ssize_t on_name_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    // Check authentication
    if (get_current_state() != STATE_AUTHENTICATED) {
        return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
    }

    // Validate offset
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    // Validate received name
    if (len == 0 || len > JOYA_SUFFIX_MAX_LEN) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    if (memchr(buf, '\0', len) != NULL) {
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    // Build full name
    char name[JOYA_NAME_MAX_LEN + 1];

    memcpy(name, JOYA_NAME_PREFIX, sizeof(JOYA_NAME_PREFIX) - 1);
    memcpy(name + sizeof(JOYA_NAME_PREFIX) - 1, buf, len);

    name[sizeof(JOYA_NAME_PREFIX) - 1 + len] = '\0';

    // Update Bluetooth device name
    int err = bt_set_name(name);
    if (err) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    // Update RAM and persist to Flash
    err = storage_save_name(name);

    if (err < 0) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    // Flash failure is non-critical (RAM was updated)
    return len;
}

static ssize_t on_name_read(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            void *buf, uint16_t len, uint16_t offset)
{
    ARG_UNUSED(attr);

    const char *name = storage_get_name();
    const size_t prefix_len = sizeof(JOYA_NAME_PREFIX) - 1;

    // No custom name configured
    if (strcmp(name, JOYA_ADV_NAME_DEFAULT) == 0) {
        return bt_gatt_attr_read(conn, attr, buf, len, offset, "", 0);
    }

    // Return only the suffix
    if (strncmp(name, JOYA_NAME_PREFIX, prefix_len) != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    const char *suffix = name + prefix_len;

    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             suffix, strlen(suffix));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

/** @brief Get a reference to the current connection.
 * @return A reference to the current connection, or NULL if not connected or notifications are not enabled.
 */
static struct bt_conn *get_current_conn_ref(void)
{
	struct bt_conn *conn = NULL;

	k_mutex_lock(&conn_mutex, K_FOREVER);

	if (current_conn && notify_enabled) {
		conn = bt_conn_ref(current_conn);
	}

	k_mutex_unlock(&conn_mutex);

	return conn;
}

static struct bt_conn *get_battery_conn_ref(bool measurement)
{
    struct bt_conn *conn = NULL;

    k_mutex_lock(&conn_mutex, K_FOREVER);
    if (current_conn && (measurement ? battery_measurement_notify_enabled :
                                      battery_change_notify_enabled)) {
        conn = bt_conn_ref(current_conn);
    }
    k_mutex_unlock(&conn_mutex);
    return conn;
}

/**
 * GATT SERVICE DEFINITIONS
 */

// COMMANDS SERVICE
BT_GATT_SERVICE_DEFINE(joya_commands_svc,
    BT_GATT_PRIMARY_SERVICE(&joya_commands_svc_uuid),

    // TX: Notifications
    BT_GATT_CHARACTERISTIC(&joya_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),

    BT_GATT_CCC(on_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    // RX: Write and Write Without Response
    BT_GATT_CHARACTERISTIC(&joya_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_rx_write, NULL)
);


// AUTHENTICATION SERVICE
BT_GATT_SERVICE_DEFINE(joya_auth_svc,
    BT_GATT_PRIMARY_SERVICE(&joya_auth_svc_uuid),

    // AUTH RX: Write and Write Without Response
    BT_GATT_CHARACTERISTIC(&joya_rx_auth_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_rx_auth_write, NULL)
);

// CONFIGURATION SERVICE
BT_GATT_SERVICE_DEFINE(joya_config_svc,
    BT_GATT_PRIMARY_SERVICE(&joya_config_svc_uuid),

    // Device Name: Read / Write
    BT_GATT_CHARACTERISTIC(&joya_name_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           on_name_read, on_name_write, NULL)
);

// BATTERY SERVICE
BT_GATT_SERVICE_DEFINE(joya_battery_svc,
    BT_GATT_PRIMARY_SERVICE(&joya_battery_svc_uuid),
    BT_GATT_CHARACTERISTIC(&joya_battery_change_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(on_battery_change_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&joya_battery_measurement_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(on_battery_measurement_ccc_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/** @brief Defines the advertising data for the Joya device.
 * General Discoverable Mode and BR/EDR not supported (LE only) 
 */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

/**
 * @brief Sends a notification to the mobile app (wrapper: ble_send_event_secure).
 * @param event_byte The 1-byte code to send.
 * @return 0 on success, or a negative error code on failure.
 */
static int ble_send_notify(struct bt_conn * conn, uint8_t event_byte){
	const struct bt_gatt_attr *attr;
	int err;

	attr = bt_gatt_find_by_uuid(joya_commands_svc.attrs,
                           joya_commands_svc.attr_count,
                           &joya_tx_uuid.uuid);
	if (!attr) {
		return -EINVAL;
	}

	err = bt_gatt_notify(conn, attr, &event_byte, sizeof(event_byte));

	return err;
}


/** 
 * PUBLIC API
 */

int ble_driver_init(void) {
    int err = ota_access_init();
    if (err) {
        return err;
    }
    err = bt_enable(NULL);
    if (err) {
        return err;
    }
    
    return 0;
}

bool ble_is_current_conn(struct bt_conn *conn)
{
    bool valid;
    k_mutex_lock(&conn_mutex, K_FOREVER);
    valid = conn && current_conn == conn;
    k_mutex_unlock(&conn_mutex);
    return valid;
}

bool ble_is_authenticated_conn(struct bt_conn *conn)
{
    bool valid;
    k_mutex_lock(&conn_mutex, K_FOREVER);
    valid = conn && current_conn == conn && current_conn_authenticated;
    k_mutex_unlock(&conn_mutex);
    return valid;
}

bool ble_mark_authenticated_conn(struct bt_conn *conn)
{
    bool valid;
    k_mutex_lock(&conn_mutex, K_FOREVER);
    valid = conn && current_conn == conn;
    if (valid) {
        current_conn_authenticated = true;
    }
    k_mutex_unlock(&conn_mutex);
    return valid;
}

int ble_start_setup_advertising(bool is_new_connection)
{
    int err;

    err = bt_le_adv_stop();
    if (err < 0 && err != -EALREADY) {
        // TODO: Handle advertising stop failure
        return err;
    }

    // Select advertising name
    if (is_new_connection) {
        err = bt_set_name(JOYA_ADV_NAME_SETUP);
    } else {
        err = bt_set_name(storage_get_name());
    }

    if (err < 0) {
        return err;
    }

    err = bt_le_adv_start(&joya_adv_param,
                          ad, ARRAY_SIZE(ad),
                          NULL, 0);

    if (err < 0) {
        return err;
    }

    return 0;
}

int ble_start_setup_advertising_backup(void)
{
	int err;

	err = bt_le_adv_stop();
	if (err < 0 && err != -EALREADY) {
		// (improvement): decide what to do if stopping advertising fails (e.g., retry, log error, etc.)
	}

	err = bt_set_name("Joya BACKUP");
	
	if (err < 0) {
		return err;
	}

	err = bt_le_adv_start(&joya_adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err < 0) {
		return err;
	}


	return 0;
}

int ble_stop_advertising(void) {
    return bt_le_adv_stop();
}


int ble_send_event_secure(uint8_t event_byte) {
    struct bt_conn *conn = get_current_conn_ref();
    if (!conn) {
        return -ENOTCONN;
    }

    int ret = ble_send_notify(conn, event_byte);
    bt_conn_unref(conn);
    return ret;
}

static int ble_send_battery_value(int32_t value, bool measurement)
{
    struct bt_conn *conn;
    const struct bt_uuid *uuid = measurement ? &joya_battery_measurement_uuid.uuid :
                                               &joya_battery_change_uuid.uuid;
    const struct bt_gatt_attr *attr;
    uint8_t payload[2];
    size_t payload_len;
    int err;

    if (measurement ? (value < 0 || value > UINT16_MAX) :
                      (value < 0 || value > 2)) {
        return -EINVAL;
    }
    payload[0] = (uint8_t)value;
    payload_len = 1;
    if (measurement) {
        payload[1] = (uint8_t)((uint32_t)value >> 8);
        payload_len = 2;
    }
    if (get_current_state() != STATE_AUTHENTICATED) {
        return -EACCES;
    }
    conn = get_battery_conn_ref(measurement);
    if (!conn) {
        return -ENOTCONN;
    }

    attr = bt_gatt_find_by_uuid(joya_battery_svc.attrs,
                                joya_battery_svc.attr_count, uuid);
    err = attr ? bt_gatt_notify(conn, attr, payload, payload_len) : -EINVAL;
    bt_conn_unref(conn);
    return err;
}

int ble_send_battery_change(uint8_t level)
{
    return ble_send_battery_value(level, false);
}

int ble_send_battery_measurement(int32_t voltage_mv)
{
    return ble_send_battery_value(voltage_mv, true);
}


int ble_disconnect(void) {
    int err = 0;
    int adv_err;
    struct bt_conn *conn = NULL;

    k_mutex_lock(&conn_mutex, K_FOREVER);

    if(current_conn) {
        conn = bt_conn_ref(current_conn);
    }
    
    k_mutex_unlock(&conn_mutex);

    if (conn) {
        err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        bt_conn_unref(conn);

        if (err) {
            // (improvement): decide what to do if disconnection fails (e.g., retry, log error, etc.)
        }

        /*
        * bt_conn_disconnect() only starts the disconnection procedure.
        * The registered disconnected callback is called when the link is actually
        * terminated; current_conn is released and cleared there.
        */
    }

    adv_err = bt_le_adv_stop();
    if (adv_err == -EALREADY) {
        // Advertising was already stopped, which is fine
        adv_err = 0;
    } else if (adv_err) {
        // (improvement): decide what to do if stopping advertising fails (e.g., retry, log error, etc.)
    }

    return err ? err : adv_err;
}
