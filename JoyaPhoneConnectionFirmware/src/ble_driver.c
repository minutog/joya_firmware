#include "ble_driver.h"
#include <zephyr/sys/printk.h>

#define JOYA_ADV_NAME_SETUP      "Joya Setup"
#define JOYA_ADV_NAME_RECONNECT  "Joya"


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

static K_MUTEX_DEFINE(conn_mutex);

/**
 * CALLBACKS (see app_comm.c)
 */

static void on_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    bool enabled = (value == BT_GATT_CCC_NOTIFY);

    k_mutex_lock(&conn_mutex, K_FOREVER);
    notify_enabled = enabled;
    k_mutex_unlock(&conn_mutex);

    printk("BLE CCC changed: notify_enabled=%d raw=0x%04x\n", enabled, value);
        
    on_ccc_changed_handler(attr, value);
}

static ssize_t on_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    const uint8_t *bytes = buf;
    printk("BLE RX write: len=%u offset=%u first=0x%02x\n",
           len, offset, len > 0 ? bytes[0] : 0);
                            
    return on_rx_write_handler(conn, attr, buf, len, offset, flags);
}

static ssize_t on_rx_auth_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    printk("BLE RX AUTH write: len=%u offset=%u\n", len, offset);
    return on_rx_auth_write_handler(conn, attr, buf, len, offset, flags);
}


static void on_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        printk("BLE connection failed: err=%u\n", err);
        return;
    }

    printk("BLE connected\n");

    k_mutex_lock(&conn_mutex, K_FOREVER);
    if(current_conn){
        k_mutex_unlock(&conn_mutex);
        printk("BLE rejecting extra connection\n");
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return;
    }

    current_conn = bt_conn_ref(conn);
    notify_enabled = false;

    k_mutex_unlock(&conn_mutex);


    on_connected_handler(conn, err);
}


static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    struct bt_conn *old_conn = NULL;
    k_mutex_lock(&conn_mutex, K_FOREVER);

    if(current_conn == conn) {
        old_conn = current_conn;
        current_conn = NULL;
    }

    notify_enabled = false;
    k_mutex_unlock(&conn_mutex);

    if(old_conn) {
        bt_conn_unref(old_conn);
    }

    printk("BLE disconnected: reason=0x%02x\n", reason);

    on_disconnected_handler(conn, reason);
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

/**
 * GATT SERVICE DEFINITION
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

    // RX AUTH Characteristic UUID: supports Write and Write Without Response (improvement: maybe add some security requirements)
    BT_GATT_CHARACTERISTIC(&joya_rx_auth_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_rx_auth_write, NULL)
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

	attr = bt_gatt_find_by_uuid(joya_svc.attrs,
				    joya_svc.attr_count,
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
    int err = bt_enable(NULL);
    if (err) {
        printk("bt_enable failed: %d\n", err);
        return err;
    }

    printk("bt_enable OK\n");
    
    return 0;
}

int ble_start_setup_advertising(void)
{
	int err;

	err = bt_le_adv_stop();
	if (err < 0 && err != -EALREADY) {
		printk("Stopping advertising before setup failed: %d\n", err);
	}

	err = bt_set_name(JOYA_ADV_NAME_SETUP);
	if (err < 0) {
		printk("bt_set_name(%s) failed: %d\n", JOYA_ADV_NAME_SETUP, err);
		return err;
	}

	err = bt_le_adv_start(&joya_adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err < 0) {
		printk("Advertising start failed: name=%s err=%d\n", JOYA_ADV_NAME_SETUP, err);
		return err;
	}

	printk("Advertising started: name=%s\n", JOYA_ADV_NAME_SETUP);

	return 0;
}

int ble_start_reconnect_advertising(void)
{
	int err;

	err = bt_le_adv_stop();
	if (err < 0 && err != -EALREADY) {
		printk("Stopping advertising before reconnect failed: %d\n", err);
	}

	err = bt_set_name(JOYA_ADV_NAME_RECONNECT);
	if (err < 0) {
		printk("bt_set_name(%s) failed: %d\n", JOYA_ADV_NAME_RECONNECT, err);
		return err;
	}

	err = bt_le_adv_start(&joya_adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err < 0) {
		printk("Advertising start failed: name=%s err=%d\n", JOYA_ADV_NAME_RECONNECT, err);
		return err;
	}

	printk("Advertising started: name=%s\n", JOYA_ADV_NAME_RECONNECT);

	return 0;
}


int ble_stop_advertising(void) {
    int err = bt_le_adv_stop();
    printk("Advertising stop requested: %d\n", err);
    return err;
}


int ble_send_event_secure(uint8_t event_byte) {
    struct bt_conn *conn = get_current_conn_ref();
    if (!conn) {
        printk("BLE notify skipped: no connected/notifying central event=0x%02x\n", event_byte);
        return -ENOTCONN;
    }

    int ret = ble_send_notify(conn, event_byte);
    printk("BLE notify event=0x%02x ret=%d\n", event_byte, ret);
    bt_conn_unref(conn);
    return ret;
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
            printk("BLE disconnect failed: %d\n", err);
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
        printk("Advertising stop during disconnect failed: %d\n", adv_err);
    }

    printk("BLE disconnect requested: conn_err=%d adv_err=%d\n", err, adv_err);
    return err ? err : adv_err;
}
