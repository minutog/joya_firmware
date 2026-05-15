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
#define BUTTON_CLICK_WINDOW_MS 600
#define BUTTON_ROUTINE_CANCEL_HOLD_MS 900
#define BUTTON_FACTORY_RESET_HOLD_MS 15000

#define DRV2605_I2C_ADDR_LOW 0x5A
#define DRV2605_I2C_ADDR_HIGH 0x5B
#define DRV2605_REG_MODE 0x01
#define DRV2605_REG_RTP_INPUT 0x02
#define DRV2605_REG_LIBRARY 0x03
#define DRV2605_REG_WAVESEQ1 0x04
#define DRV2605_REG_WAVESEQ2 0x05
#define DRV2605_REG_GO 0x0C
#define DRV2605_MODE_INTERNAL_TRIGGER 0x00
#define DRV2605_MODE_RTP 0x05
#define HAPTIC_RTP_MAX 127
#define HAPTIC_RAMP_STEP_MS 250

enum adv_request {
	ADV_REQUEST_NONE = 0,
	ADV_REQUEST_SETUP,
	ADV_REQUEST_RECONNECT,
};

enum haptic_pattern {
	HAPTIC_PATTERN_NONE = 0,
	HAPTIC_PATTERN_PAIRING_WAKE,
	HAPTIC_PATTERN_ROUTINE_START,
	HAPTIC_PATTERN_ROUTINE_CANCEL,
	HAPTIC_PATTERN_EMERGENCY_START,
};

struct haptic_step {
	uint16_t duration_ms;
	uint8_t amplitude;
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
static bool factory_reset_in_progress;
static bool factory_reset_triggered;
static uint8_t button_click_count;
static uint16_t drv2605_addr = DRV2605_I2C_ADDR_LOW;
static char claimed_app_id[JOYA_APP_ID_MAX_LEN + 1];
static enum adv_request pending_adv_request = ADV_REQUEST_NONE;
static enum haptic_pattern active_haptic_pattern = HAPTIC_PATTERN_NONE;
static size_t haptic_step_index;

static void advertise_work_handler(struct k_work *work);
static void setup_timeout_handler(struct k_work *work);
static void button_work_handler(struct k_work *work);
static void click_eval_handler(struct k_work *work);
static void factory_reset_hold_handler(struct k_work *work);
static void haptic_pattern_work_handler(struct k_work *work);

static K_WORK_DEFINE(advertise_work, advertise_work_handler);
static K_WORK_DELAYABLE_DEFINE(setup_timeout_work, setup_timeout_handler);
static K_WORK_DEFINE(button_work, button_work_handler);
static K_WORK_DELAYABLE_DEFINE(click_eval_work, click_eval_handler);
static K_WORK_DELAYABLE_DEFINE(factory_reset_hold_work, factory_reset_hold_handler);
static K_WORK_DELAYABLE_DEFINE(haptic_pattern_work, haptic_pattern_work_handler);

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

static const uint8_t haptic_pairing_ramp[] = {
	0, 1, 2, 3, 5, 8, 12, 18, 27, 40, 58, 78, 96, 111, 121, HAPTIC_RTP_MAX,
};

static const uint8_t haptic_cancel_ramp[] = {
	HAPTIC_RTP_MAX, 110, 95, 78, 62, 47, 35, 25, 17, 11, 7, 4, 2, 1, 0, 0,
};

static const struct haptic_step haptic_routine_steps[] = {
	{ 90, 104 }, { 70, 0 }, { 90, 104 }, { 180, 0 },
	{ 110, 108 }, { 160, 0 }, { 320, 120 },
};

static const struct haptic_step haptic_emergency_steps[] = {
	{ 90, 114 }, { 80, 0 }, { 120, HAPTIC_RTP_MAX }, { 160, 0 },
	{ 90, 114 }, { 80, 0 }, { 120, HAPTIC_RTP_MAX }, { 1000, 0 },
	{ 90, 114 }, { 80, 0 }, { 120, HAPTIC_RTP_MAX }, { 160, 0 },
	{ 90, 114 }, { 80, 0 }, { 120, HAPTIC_RTP_MAX },
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

	err = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL_TRIGGER);
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

static int haptic_ensure_ready(void)
{
	int err;

	if (!haptic_ready) {
		err = haptic_init();
		if (err < 0) {
			return err;
		}
	}

	return 0;
}

static void haptic_idle(void)
{
	if (!haptic_ready) {
		active_haptic_pattern = HAPTIC_PATTERN_NONE;
		haptic_step_index = 0;
		return;
	}

	(void)drv2605_write_reg(DRV2605_REG_RTP_INPUT, 0);
	(void)drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL_TRIGGER);
	active_haptic_pattern = HAPTIC_PATTERN_NONE;
	haptic_step_index = 0;
}

static void haptic_cancel_pattern(void)
{
	k_work_cancel_delayable(&haptic_pattern_work);
	haptic_idle();
}

static bool haptic_set_rtp(uint8_t amplitude)
{
	int err;

	err = haptic_ensure_ready();
	if (err < 0) {
		return false;
	}

	err = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);
	if (err < 0) {
		printk("Haptic RTP mode failed: %d\n", err);
		haptic_ready = false;
		active_haptic_pattern = HAPTIC_PATTERN_NONE;
		return false;
	}

	err = drv2605_write_reg(DRV2605_REG_RTP_INPUT, amplitude);
	if (err < 0) {
		printk("Haptic RTP write failed: %d\n", err);
		haptic_ready = false;
		active_haptic_pattern = HAPTIC_PATTERN_NONE;
		return false;
	}

	return true;
}

static void haptic_play_effect(uint8_t effect)
{
	int err;

	err = haptic_ensure_ready();
	if (err < 0) {
		return;
	}

	haptic_cancel_pattern();
	(void)drv2605_write_reg(DRV2605_REG_WAVESEQ1, effect);
	(void)drv2605_write_reg(DRV2605_REG_WAVESEQ2, 0x00);
	(void)drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL_TRIGGER);

	err = drv2605_write_reg(DRV2605_REG_GO, 0x01);
	if (err < 0) {
		printk("Haptic trigger failed: %d\n", err);
		haptic_ready = false;
	}
}

static bool haptic_get_next_step(uint8_t *amplitude, uint16_t *duration_ms)
{
	switch (active_haptic_pattern) {
	case HAPTIC_PATTERN_PAIRING_WAKE:
		if (haptic_step_index >= ARRAY_SIZE(haptic_pairing_ramp)) {
			return false;
		}
		*amplitude = haptic_pairing_ramp[haptic_step_index];
		*duration_ms = HAPTIC_RAMP_STEP_MS;
		return true;
	case HAPTIC_PATTERN_ROUTINE_START:
		if (haptic_step_index >= ARRAY_SIZE(haptic_routine_steps)) {
			return false;
		}
		*amplitude = haptic_routine_steps[haptic_step_index].amplitude;
		*duration_ms = haptic_routine_steps[haptic_step_index].duration_ms;
		return true;
	case HAPTIC_PATTERN_ROUTINE_CANCEL:
		if (haptic_step_index >= ARRAY_SIZE(haptic_cancel_ramp)) {
			return false;
		}
		*amplitude = haptic_cancel_ramp[haptic_step_index];
		*duration_ms = HAPTIC_RAMP_STEP_MS;
		return true;
	case HAPTIC_PATTERN_EMERGENCY_START:
		if (haptic_step_index >= ARRAY_SIZE(haptic_emergency_steps)) {
			return false;
		}
		*amplitude = haptic_emergency_steps[haptic_step_index].amplitude;
		*duration_ms = haptic_emergency_steps[haptic_step_index].duration_ms;
		return true;
	default:
		return false;
	}
}

static void haptic_pattern_work_handler(struct k_work *work)
{
	uint8_t amplitude;
	uint16_t duration_ms;

	ARG_UNUSED(work);

	if (!haptic_get_next_step(&amplitude, &duration_ms)) {
		haptic_idle();
		return;
	}

	haptic_step_index++;
	if (!haptic_set_rtp(amplitude)) {
		return;
	}

	k_work_reschedule(&haptic_pattern_work, K_MSEC(duration_ms));
}

static void haptic_play_pattern(enum haptic_pattern pattern)
{
	if (pattern == HAPTIC_PATTERN_NONE) {
		haptic_cancel_pattern();
		return;
	}

	if (haptic_ensure_ready() < 0) {
		return;
	}

	k_work_cancel_delayable(&haptic_pattern_work);
	active_haptic_pattern = pattern;
	haptic_step_index = 0;
	k_work_schedule(&haptic_pattern_work, K_NO_WAIT);
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

static void clear_button_click_state(void)
{
	button_click_count = 0;
	k_work_cancel_delayable(&click_eval_work);
}

static void finish_phone_pairing_reset_idle(void)
{
	pending_adv_request = ADV_REQUEST_NONE;
	setup_window_open = false;
	k_work_cancel_delayable(&setup_timeout_work);
	(void)bt_le_adv_stop();
	printk("Phone pairing reset complete; waiting for button double click\n");
}

static void delete_setting_key(const char *key)
{
	int err;

	err = settings_delete(key);
	if (err < 0) {
		printk("Settings delete failed for %s: %d\n", key, err);
	}
}

static void reset_phone_pairing_state(void)
{
	struct bt_conn *conn = NULL;
	int err;

	printk("Button event: phone pairing reset / 15s hold\n");

	factory_reset_in_progress = true;
	clear_button_click_state();
	k_work_cancel_delayable(&setup_timeout_work);
	(void)bt_le_adv_stop();
	nus_send_text("EVENT:PHONE_PAIRING_RESET");

	/* This clears our app-level claim. bt_unpair clears the BLE bond keys
	 * stored by the Bluetooth stack, so the next phone setup is fresh.
	 */
	claimed = false;
	memset(claimed_app_id, 0, sizeof(claimed_app_id));
	delete_setting_key("joya/app_id");
	delete_setting_key("joya/claimed");

	err = bt_unpair(BT_ID_DEFAULT, NULL);
	if (err < 0) {
		printk("BLE bond clear failed: %d\n", err);
	}

	haptic_play_effect(0x2F);

	if (current_conn != NULL) {
		conn = bt_conn_ref(current_conn);
	}

	if (conn != NULL) {
		err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(conn);
		if (err == 0) {
			return;
		}

		printk("BLE disconnect after reset failed: %d\n", err);
	}

	factory_reset_in_progress = false;
	finish_phone_pairing_reset_idle();
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

	haptic_play_effect(0x01);
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
	clear_button_click_state();
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
	clear_button_click_state();

	if (factory_reset_in_progress) {
		factory_reset_in_progress = false;
		finish_phone_pairing_reset_idle();
		return;
	}

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
	uint8_t count = button_click_count;

	ARG_UNUSED(work);

	button_click_count = 0;

	if (count == 0) {
		return;
	}

	printk("Button click window closed, count=%u\n", count);

	if (current_conn == NULL) {
		if (count >= 2) {
			printk("Button event: wake BLE / double click\n");
			haptic_play_pattern(HAPTIC_PATTERN_PAIRING_WAKE);
			request_advertising(claimed ? ADV_REQUEST_RECONNECT : ADV_REQUEST_SETUP);
		} else {
			printk("Button single click ignored while disconnected\n");
		}
		return;
	}

	if (count == 1) {
		printk("Button event: routine start / single click\n");
		haptic_play_pattern(HAPTIC_PATTERN_ROUTINE_START);
		nus_send_text("EVENT:ROUTINE_START");
	} else if (count == 2) {
		printk("Button double click ignored while connected\n");
	} else {
		printk("Button event: emergency start / triple click\n");
		haptic_play_pattern(HAPTIC_PATTERN_EMERGENCY_START);
		nus_send_text("EVENT:EMERGENCY_START");
	}
}

static void factory_reset_hold_handler(struct k_work *work)
{
	int button_active;

	ARG_UNUSED(work);

	button_active = gpio_pin_get_dt(&button);
	if (button_active < 0) {
		printk("Button read failed during reset hold: %d\n", button_active);
		return;
	}

	if (!button_active) {
		return;
	}

	factory_reset_triggered = true;
	reset_phone_pairing_state();
}

static void button_work_handler(struct k_work *work)
{
	static int64_t last_edge_ms;
	static int64_t press_start_ms;
	static bool pressed;
	int64_t now = k_uptime_get();
	int64_t press_duration_ms;
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
		if (!pressed) {
			pressed = true;
			press_start_ms = now;
			factory_reset_triggered = false;
			k_work_reschedule(&factory_reset_hold_work,
					  K_MSEC(BUTTON_FACTORY_RESET_HOLD_MS));
		}
		return;
	}

	if (!pressed) {
		return;
	}

	pressed = false;
	k_work_cancel_delayable(&factory_reset_hold_work);

	if (factory_reset_triggered) {
		factory_reset_triggered = false;
		return;
	}

	press_duration_ms = now - press_start_ms;

	if (press_duration_ms >= BUTTON_FACTORY_RESET_HOLD_MS) {
		factory_reset_triggered = true;
		reset_phone_pairing_state();
		return;
	}

	if (current_conn == NULL) {
		/* While disconnected, the button is only a BLE wake control.
		 * A single click is ignored so setup cannot start by accident.
		 */
		if (press_duration_ms >= BUTTON_ROUTINE_CANCEL_HOLD_MS) {
			printk("Button hold ignored while disconnected\n");
			clear_button_click_state();
			return;
		}

		button_click_count++;

		if (button_click_count >= 2) {
			clear_button_click_state();
			printk("Button event: wake BLE / double click\n");
			haptic_play_pattern(HAPTIC_PATTERN_PAIRING_WAKE);
			request_advertising(claimed ? ADV_REQUEST_RECONNECT : ADV_REQUEST_SETUP);
		} else {
			k_work_reschedule(&click_eval_work, K_MSEC(BUTTON_CLICK_WINDOW_MS));
			printk("Button click while disconnected; waiting for double click\n");
		}

		return;
	}

	if (press_duration_ms >= BUTTON_ROUTINE_CANCEL_HOLD_MS) {
		clear_button_click_state();
		printk("Button event: routine cancel / hold\n");
		haptic_play_pattern(HAPTIC_PATTERN_ROUTINE_CANCEL);
		nus_send_text("EVENT:ROUTINE_CANCEL");
		return;
	}

	button_click_count++;

	if (button_click_count >= 3) {
		clear_button_click_state();
		printk("Button event: emergency start / triple click\n");
		haptic_play_pattern(HAPTIC_PATTERN_EMERGENCY_START);
		nus_send_text("EVENT:EMERGENCY_START");
		return;
	}

	k_work_reschedule(&click_eval_work, K_MSEC(BUTTON_CLICK_WINDOW_MS));
	printk("Button click while connected; waiting for click window\n");
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
