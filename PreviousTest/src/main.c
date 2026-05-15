#include <errno.h>
#include <stdbool.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define BUTTON_NODE DT_PATH(zephyr_user)

#if !DT_NODE_HAS_PROP(BUTTON_NODE, button_gpios)
#error "button_gpios is not defined in app.overlay"
#endif

#if !DT_NODE_HAS_PROP(BUTTON_NODE, haptic_en_gpios)
#error "haptic_en_gpios is not defined in app.overlay"
#endif

#define DRV2605_I2C_ADDR_LOW 0x5A
#define DRV2605_I2C_ADDR_HIGH 0x5B
#define DRV2605_REG_MODE 0x01
#define DRV2605_REG_LIBRARY 0x03
#define DRV2605_REG_WAVESEQ1 0x04
#define DRV2605_REG_WAVESEQ2 0x05
#define DRV2605_REG_WAVESEQ3 0x06
#define DRV2605_REG_WAVESEQ4 0x07
#define DRV2605_REG_WAVESEQ5 0x08
#define DRV2605_REG_WAVESEQ6 0x09
#define DRV2605_REG_GO 0x0C

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, button_gpios);
static const struct gpio_dt_spec haptic_en = GPIO_DT_SPEC_GET(BUTTON_NODE, haptic_en_gpios);
static struct gpio_callback button_cb_data;
static struct k_work button_send_work;
static const struct device *haptic_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static bool haptic_ready;
static uint16_t drv2605_addr = DRV2605_I2C_ADDR_LOW;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

static void nus_notify_changed(bool enabled, void *ctx)
{
	ARG_UNUSED(ctx);
	printk("NUS notify: %s\n", enabled ? "enabled" : "disabled");
}

static struct bt_nus_cb nus_cb = {
	.notif_enabled = nus_notify_changed,
};

static int drv2605_probe_addr(uint16_t addr)
{
	uint8_t status;

	return i2c_reg_read_byte(haptic_i2c, addr, 0x00, &status);
}

static int drv2605_write_reg(uint8_t reg, uint8_t value)
{
	uint8_t data[2] = {reg, value};

	return i2c_write(haptic_i2c, data, sizeof(data), drv2605_addr);
}

static int haptic_init(void)
{
	int err;

	if (!device_is_ready(haptic_i2c)) {
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&haptic_en)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&haptic_en, GPIO_OUTPUT_ACTIVE);
	if (err < 0) {
		return err;
	}

	k_msleep(10);

	/* Recover a potentially stuck I2C bus after resets/power sequencing. */
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
			printk("DRV2605 probe failed at 0x5A and 0x5B (err %d)\n", err);
				printk("Check DRV power (VDD/VBAT), EN on P0.7, and SDA/SCL pull-ups.\n");
			return err;
		}
	}

	printk("DRV2605 detected at 0x%02x\n", drv2605_addr);

	err = drv2605_write_reg(DRV2605_REG_MODE, 0x00);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_LIBRARY, 0x01);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_WAVESEQ1, 0x01);
	if (err < 0) {
		return err;
	}

	/* Complex pattern: strong click -> double click -> buzz -> sharp tick. */
	err = drv2605_write_reg(DRV2605_REG_WAVESEQ2, 0x0A);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_WAVESEQ3, 0x2F);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_WAVESEQ4, 0x0C);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_WAVESEQ5, 0x00);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_WAVESEQ6, 0x00);
	if (err < 0) {
		return err;
	}

	haptic_ready = true;
	return 0;
}

static void haptic_play_once(void)
{
	int err;

	if (!haptic_ready) {
		err = haptic_init();
		if (err < 0) {
			printk("Haptic init retry failed: %d\n", err);
			return;
		}
	}

	err = drv2605_write_reg(DRV2605_REG_GO, 0x01);
	if (err < 0) {
		printk("Haptic trigger failed: %d\n", err);
		haptic_ready = false;
	}
}

static void button_send_work_handler(struct k_work *work)
{
	static const char msg[] = "1";
	int err;

	ARG_UNUSED(work);

	haptic_play_once();

	err = bt_nus_send(NULL, msg, sizeof(msg) - 1);
	if (err == 0) {
		printk("Sent BLE message: 1\n");
	}
}

static void button_pressed_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit(&button_send_work);
}

static int button_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed_isr, BIT(button.pin));
	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

int main(void)
{
	int err;

	k_work_init(&button_send_work, button_send_work_handler);

	err = button_init();
	if (err != 0) {
		printk("Button init failed: %d\n", err);
		return 0;
	}

	err = haptic_init();
	if (err != 0) {
		haptic_ready = false;
		printk("Haptic init failed: %d (BLE will still start)\n", err);
	}

	err = bt_nus_cb_register(&nus_cb, NULL);
	if (err != 0) {
		printk("NUS callback register failed: %d\n", err);
		return 0;
	}

	err = bt_enable(NULL);
	if (err != 0) {
		printk("Bluetooth enable failed: %d\n", err);
		return 0;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err != 0) {
		printk("Advertising start failed: %d\n", err);
		return 0;
	}

	printk("Ready: button -> BLE + haptic\n");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
