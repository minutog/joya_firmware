#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define BUTTON_NODE DT_PATH(zephyr_user)

#if !DT_NODE_HAS_PROP(BUTTON_NODE, button_gpios)
#error "button_gpios is not defined in app.overlay"
#endif

#if !DT_NODE_HAS_PROP(BUTTON_NODE, haptic_en_gpios)
#error "haptic_en_gpios is not defined in app.overlay"
#endif

#define JOYA_ID "JOYA-DEV-001"
#define JOYA_NAME "Joya"
#define JOYA_SETUP_NAME "Joya Setup"
#define JOYA_APP_ID_MAX_LEN 32
#define SETUP_WINDOW K_SECONDS(90)
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_CLICK_WINDOW_MS 450
#define BUTTON_HOLD_MS 900

#define DRV2605_I2C_ADDR_LOW 0x5A
#define DRV2605_I2C_ADDR_HIGH 0x5B
#define DRV2605_REG_MODE 0x01
#define DRV2605_REG_LIBRARY 0x03
#define DRV2605_REG_WAVESEQ1 0x04
#define DRV2605_REG_WAVESEQ2 0x05
#define DRV2605_REG_GO 0x0C

enum adv_request {
	ADV_REQUEST_NONE = 0,
	ADV_REQUEST_SETUP,
	ADV_REQUEST_RECONNECT,
};

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, button_gpios);
static const struct gpio_dt_spec haptic_en = GPIO_DT_SPEC_GET(BUTTON_NODE, haptic_en_gpios);
static const struct device *haptic_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static struct gpio_callback button_cb_data;
static struct bt_conn *current_conn;

static bool claimed;
static bool setup_window_open;
static bool notifications_enabled;
static bool haptic_ready;
static uint8_t button_click_count;
static uint16_t drv2605_addr = DRV2605_I2C_ADDR_LOW;
static char claimed_app_id[JOYA_APP_ID_MAX_LEN + 1];
static enum adv_request pending_adv_request = ADV_REQUEST_NONE;

static void advertise_work_handler(struct k_work *work);
static void setup_timeout_handler(struct k_work *work);
static void button_work_handler(struct k_work *work);
static void click_eval_handler(struct k_work *work);

static K_WORK_DEFINE(advertise_work, advertise_work_handler);
static K_WORK_DELAYABLE_DEFINE(setup_timeout_work, setup_timeout_handler);
static K_WORK_DEFINE(button_work, button_work_handler);
static K_WORK_DELAYABLE_DEFINE(click_eval_work, click_eval_handler);

static const struct bt_data ad_setup[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, JOYA_SETUP_NAME, sizeof(JOYA_SETUP_NAME) - 1),
};

static const struct bt_data ad_reconnect[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, JOYA_NAME, sizeof(JOYA_NAME) - 1),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

static int drv2605_probe_addr(uint16_t addr)
{
	uint8_t status;

	return i2c_reg_read_byte(haptic_i2c, addr, 0x00, &status);
}

static int drv2605_write_reg(uint8_t reg, uint8_t value)
{
	uint8_t data[2] = { reg, value };

	return i2c_write(haptic_i2c, data, sizeof(data), drv2605_addr);
}

static int haptic_init(void)
{
	int err;

	if (!device_is_ready(haptic_i2c) || !gpio_is_ready_dt(&haptic_en)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&haptic_en, GPIO_OUTPUT_ACTIVE);
	if (err < 0) {
		return err;
	}

	k_msleep(10);

	err = i2c_recover_bus(haptic_i2c);
	if (err < 0 && err != -ENOSYS) {
		printk("I2C recover failed: %d\n", err);
	}

	drv2605_addr = DRV2605_I2C_ADDR_LOW;
	err = drv2605_probe_addr(drv2605_addr);
	if (err < 0) {
		drv2605_addr = DRV2605_I2C_ADDR_HIGH;
		err = drv2605_probe_addr(drv2605_addr);
		if (err < 0) {
			printk("DRV2605 not detected; BLE test continues without haptic\n");
			return err;
		}
	}

	err = drv2605_write_reg(DRV2605_REG_MODE, 0x00);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_LIBRARY, 0x01);
	if (err < 0) {
		return err;
	}

	haptic_ready = true;
	printk("DRV2605 ready at 0x%02x\n", drv2605_addr);
	return 0;
}

static void haptic_play(uint8_t effect)
{
	int err;

	if (!haptic_ready) {
		err = haptic_init();
		if (err < 0) {
			return;
		}
	}

	/* Real product firmware will map semantic patterns to effect sequences.
	 * For this connection test we use one short effect per event.
	 */
	(void)drv2605_write_reg(DRV2605_REG_WAVESEQ1, effect);
	(void)drv2605_write_reg(DRV2605_REG_WAVESEQ2, 0x00);

	err = drv2605_write_reg(DRV2605_REG_GO, 0x01);
	if (err < 0) {
		printk("Haptic trigger failed: %d\n", err);
		haptic_ready = false;
	}
}

static void nus_send_text(const char *text)
{
	int err;

	if (current_conn == NULL || !notifications_enabled) {
		printk("NUS skip, no subscribed peer: %s\n", text);
		return;
	}

	err = bt_nus_send(current_conn, text, strlen(text));
	if (err < 0) {
		printk("NUS send failed: %d (%s)\n", err, text);
	}
}

static void request_advertising(enum adv_request request)
{
	pending_adv_request = request;
	k_work_submit(&advertise_work);
}

static void advertise_work_handler(struct k_work *work)
{
	const struct bt_data *ad;
	size_t ad_len;
	int err;

	ARG_UNUSED(work);

	if (current_conn != NULL || pending_adv_request == ADV_REQUEST_NONE) {
		return;
	}

	(void)bt_le_adv_stop();

	if (pending_adv_request == ADV_REQUEST_SETUP) {
		ad = ad_setup;
		ad_len = ARRAY_SIZE(ad_setup);
		setup_window_open = true;
		k_work_reschedule(&setup_timeout_work, SETUP_WINDOW);
		printk("Advertising for setup as \"%s\"\n", JOYA_SETUP_NAME);
	} else {
		ad = ad_reconnect;
		ad_len = ARRAY_SIZE(ad_reconnect);
		setup_window_open = false;
		k_work_cancel_delayable(&setup_timeout_work);
		printk("Advertising for reconnect as \"%s\"\n", JOYA_NAME);
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ad_len, sd, ARRAY_SIZE(sd));
	if (err < 0) {
		printk("Advertising start failed: %d\n", err);
	}
}

static void setup_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (claimed || current_conn != NULL || !setup_window_open) {
		return;
	}

	setup_window_open = false;
	(void)bt_le_adv_stop();
	printk("Setup window expired; BLE advertising stopped\n");
}

static int save_claim(const char *app_id)
{
	uint8_t claimed_value = 1;
	int err;

	memset(claimed_app_id, 0, sizeof(claimed_app_id));
	if (app_id != NULL && app_id[0] != '\0') {
		strncpy(claimed_app_id, app_id, JOYA_APP_ID_MAX_LEN);
	} else {
		strncpy(claimed_app_id, "unknown", JOYA_APP_ID_MAX_LEN);
	}

	err = settings_save_one("joya/app_id", claimed_app_id, strlen(claimed_app_id) + 1);
	if (err < 0) {
		return err;
	}

	err = settings_save_one("joya/claimed", &claimed_value, sizeof(claimed_value));
	if (err < 0) {
		return err;
	}

	claimed = true;
	setup_window_open = false;
	k_work_cancel_delayable(&setup_timeout_work);
	printk("Claim saved for app_id=%s\n", claimed_app_id);
	return 0;
}

static void handle_claim_command(const char *app_id)
{
	int err;

	if (claimed) {
		nus_send_text("ERR:ALREADY_CLAIMED");
		return;
	}

	err = save_claim(app_id);
	if (err < 0) {
		printk("Claim save failed: %d\n", err);
		nus_send_text("ERR:CLAIM_SAVE_FAILED");
		return;
	}

	haptic_play(0x01);
	nus_send_text("CLAIM_OK:" JOYA_ID);
}

static void trim_line_end(char *text)
{
	size_t len = strlen(text);

	while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n')) {
		text[len - 1] = '\0';
		len--;
	}
}

static void nus_received(struct bt_conn *conn, const void *data, uint16_t len, void *ctx)
{
	char cmd[80];
	size_t cmd_len;

	ARG_UNUSED(conn);
	ARG_UNUSED(ctx);

	cmd_len = MIN((size_t)len, sizeof(cmd) - 1);
	memcpy(cmd, data, cmd_len);
	cmd[cmd_len] = '\0';
	trim_line_end(cmd);

	printk("NUS received: %s\n", cmd);

	if (strcmp(cmd, "PING") == 0) {
		nus_send_text(claimed ? "PONG:claimed=1" : "PONG:claimed=0");
	} else if (strncmp(cmd, "CLAIM:", strlen("CLAIM:")) == 0) {
		handle_claim_command(cmd + strlen("CLAIM:"));
	} else if (strcmp(cmd, "CANCEL_ROUTINE") == 0) {
		nus_send_text("ACK:CANCEL_ROUTINE");
	} else if (strcmp(cmd, "CANCEL_EMERGENCY") == 0) {
		nus_send_text("ACK:CANCEL_EMERGENCY");
	} else {
		nus_send_text("ERR:UNKNOWN_COMMAND");
	}
}

static void nus_notify_changed(bool enabled, void *ctx)
{
	ARG_UNUSED(ctx);

	notifications_enabled = enabled;
	printk("NUS notifications %s\n", enabled ? "enabled" : "disabled");

	if (enabled) {
		nus_send_text(claimed ? "HELLO:JOYA:claimed=1" : "HELLO:JOYA:claimed=0");
	}
}

static struct bt_nus_cb nus_cb = {
	.notif_enabled = nus_notify_changed,
	.received = nus_received,
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		printk("BLE connection failed: 0x%02x\n", err);
		request_advertising(claimed ? ADV_REQUEST_RECONNECT : ADV_REQUEST_SETUP);
		return;
	}

	current_conn = bt_conn_ref(conn);
	notifications_enabled = false;
	printk("BLE connected\n");

	/* Trigger bonding/encryption. The app can still use the simple NUS
	 * protocol, but pairing keys are stored by the Bluetooth stack.
	 */
	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err < 0) {
		printk("Security request failed: %d\n", err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	printk("BLE disconnected: 0x%02x\n", reason);

	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	notifications_enabled = false;
	request_advertising(claimed ? ADV_REQUEST_RECONNECT : ADV_REQUEST_SETUP);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	ARG_UNUSED(conn);

	if (err != 0) {
		printk("BLE security failed: %d\n", err);
		return;
	}

	printk("BLE security level: %u\n", level);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(feat);

	/* First prototype rule:
	 * - If not claimed, allow first phone to pair and claim.
	 * - If claimed, reject new pairing attempts. The already bonded phone can
	 *   reconnect without pairing again.
	 */
	if (claimed) {
		printk("Pairing rejected because Joya is already claimed\n");
		return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	}

	printk("Pairing accepted for setup\n");
	return BT_SECURITY_ERR_SUCCESS;
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	ARG_UNUSED(conn);

	printk("Pairing complete, bonded=%d\n", bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	ARG_UNUSED(conn);

	printk("Pairing failed: %d\n", reason);
}

static const struct bt_conn_auth_cb auth_callbacks = {
	.pairing_accept = pairing_accept,
};

static struct bt_conn_auth_info_cb auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static int joya_settings_set(const char *key, size_t len, settings_read_cb read_cb,
			     void *cb_arg)
{
	int rc;

	if (strcmp(key, "claimed") == 0) {
		uint8_t stored_claimed = 0;

		if (len != sizeof(stored_claimed)) {
			return -EINVAL;
		}

		rc = read_cb(cb_arg, &stored_claimed, sizeof(stored_claimed));
		if (rc < 0) {
			return rc;
		}

		claimed = stored_claimed != 0;
		return 0;
	}

	if (strcmp(key, "app_id") == 0) {
		size_t read_len = MIN(len, sizeof(claimed_app_id) - 1);

		memset(claimed_app_id, 0, sizeof(claimed_app_id));
		rc = read_cb(cb_arg, claimed_app_id, read_len);
		if (rc < 0) {
			return rc;
		}

		claimed_app_id[read_len] = '\0';
		return 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(joya, "joya", NULL, joya_settings_set, NULL, NULL);

static void click_eval_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (button_click_count > 0) {
		printk("Button click window closed, count=%u\n", button_click_count);
	}
	button_click_count = 0;
}

static void button_work_handler(struct k_work *work)
{
	static int64_t last_edge_ms;
	static int64_t press_start_ms;
	static bool pressed;
	int64_t now = k_uptime_get();
	int button_active;

	ARG_UNUSED(work);

	if ((now - last_edge_ms) < BUTTON_DEBOUNCE_MS) {
		return;
	}

	last_edge_ms = now;
	button_active = gpio_pin_get_dt(&button);
	if (button_active < 0) {
		printk("Button read failed: %d\n", button_active);
		return;
	}

	printk("Button edge: %s\n", button_active ? "pressed" : "released");

	if (button_active) {
		pressed = true;
		press_start_ms = now;
		return;
	}

	if (!pressed) {
		return;
	}

	pressed = false;

	if (current_conn == NULL) {
		/* While disconnected, the button is only a BLE wake control.
		 * A single click is ignored so setup cannot start by accident.
		 */
		if ((now - press_start_ms) >= BUTTON_HOLD_MS) {
			printk("Button hold ignored while disconnected\n");
			button_click_count = 0;
			k_work_cancel_delayable(&click_eval_work);
			return;
		}

		button_click_count++;
		k_work_reschedule(&click_eval_work, K_MSEC(BUTTON_CLICK_WINDOW_MS));

		if (button_click_count >= 2) {
			button_click_count = 0;
			k_work_cancel_delayable(&click_eval_work);
			printk("Button event: wake BLE / double click\n");
			haptic_play(0x0A);
			request_advertising(claimed ? ADV_REQUEST_RECONNECT : ADV_REQUEST_SETUP);
		} else {
			printk("Button click while disconnected; waiting for double click\n");
		}

		return;
	}

	if ((now - press_start_ms) >= BUTTON_HOLD_MS) {
		button_click_count = 0;
		k_work_cancel_delayable(&click_eval_work);
		printk("Button event: routine cancel / hold\n");
		haptic_play(0x0C);
		nus_send_text("EVENT:ROUTINE_CANCEL");
		return;
	}

	button_click_count++;
	k_work_reschedule(&click_eval_work, K_MSEC(BUTTON_CLICK_WINDOW_MS));

	if (button_click_count >= 3) {
		button_click_count = 0;
		k_work_cancel_delayable(&click_eval_work);
		printk("Button event: emergency start / triple click\n");
		haptic_play(0x2F);
		nus_send_text("EVENT:EMERGENCY_START");
		return;
	}

	if (button_click_count == 1) {
		printk("Button event: routine start / single click\n");
		haptic_play(0x01);
		nus_send_text("EVENT:ROUTINE_START");
	}
}

static void button_pressed_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit(&button_work);
}

static int button_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err < 0) {
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (err < 0) {
		return err;
	}

	gpio_init_callback(&button_cb_data, button_pressed_isr, BIT(button.pin));
	err = gpio_add_callback(button.port, &button_cb_data);
	if (err < 0) {
		return err;
	}

	return 0;
}

int main(void)
{
	int err;

	printk("Joya pairing test firmware booting\n");

	err = settings_subsys_init();
	if (err < 0) {
		printk("Settings init failed: %d\n", err);
		return 0;
	}

	err = button_init();
	if (err < 0) {
		printk("Button init failed: %d\n", err);
		return 0;
	}

	err = haptic_init();
	if (err < 0) {
		haptic_ready = false;
	}

	err = bt_conn_auth_cb_register(&auth_callbacks);
	if (err < 0) {
		printk("Auth callback register failed: %d\n", err);
		return 0;
	}

	err = bt_conn_auth_info_cb_register(&auth_info_callbacks);
	if (err < 0) {
		printk("Auth info callback register failed: %d\n", err);
		return 0;
	}

	bt_conn_cb_register(&conn_callbacks);

	err = bt_nus_cb_register(&nus_cb, NULL);
	if (err < 0) {
		printk("NUS callback register failed: %d\n", err);
		return 0;
	}

	err = bt_enable(NULL);
	if (err < 0) {
		printk("Bluetooth enable failed: %d\n", err);
		return 0;
	}

	err = settings_load();
	if (err < 0) {
		printk("Settings load failed: %d\n", err);
	}

	printk("Settings loaded: claimed=%d app_id=%s\n", claimed, claimed_app_id);
	if (claimed) {
		request_advertising(ADV_REQUEST_RECONNECT);
	} else {
		/* First-time setup is intentionally user-gated by a double click. */
		printk("Waiting for button double click to open setup advertising\n");
	}

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
