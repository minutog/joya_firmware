#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include <errno.h>
#include "drv2605.h"

#define HAPTIC_NODE DT_PATH(zephyr_user)

#if !DT_NODE_EXISTS(HAPTIC_NODE)
#error "Missing /zephyr,user node in overlay"
#endif

#if !DT_NODE_HAS_PROP(HAPTIC_NODE, haptic_en_gpios)
#error "Missing haptic_en_gpios property in /zephyr,user"
#endif

#define DRV2605_I2C_ADDR_LOW             0x5A
#define DRV2605_I2C_ADDR_HIGH            0x5B

#define DRV2605_I2C_MAX_RETRIES          3
#define DRV2605_I2C_RETRY_DELAY_MS       2

#define DRV2605_REG_STATUS               0x00
#define DRV2605_REG_MODE                 0x01
#define DRV2605_REG_RTP_INPUT            0x02
#define DRV2605_REG_LIBRARY              0x03

#define DRV2605_MODE_INTERNAL_TRIGGER    0x00
#define DRV2605_MODE_RTP                 0x05
#define DRV2605_MODE_STANDBY_INTERNAL_TRIGGER	(BIT(6) | DRV2605_MODE_INTERNAL_TRIGGER)

#define DRV2605_RTP_OFF                  0

static const struct gpio_dt_spec haptic_en =
	GPIO_DT_SPEC_GET(HAPTIC_NODE, haptic_en_gpios);

static const struct device *haptic_i2c =
	DEVICE_DT_GET(DT_NODELABEL(i2c0));

static uint16_t drv2605_addr;
static bool drv2605_ready;

/**
 * @brief Write one byte to a DRV2605 register.
 * @param reg Register address to write.
 * @param value Value to write into the register.
 * @return 0 on success, or a negative error code on failure.
 */
static int drv2605_write_reg(uint8_t reg, uint8_t value)
{
	uint8_t data[2] = { reg, value };
	int err = -EIO;

	for (int attempt = 0; attempt < DRV2605_I2C_MAX_RETRIES; attempt++) {
		err = i2c_write(haptic_i2c, data, sizeof(data), drv2605_addr);
		if (err == 0) {
			return 0;
		}

		if (attempt + 1 < DRV2605_I2C_MAX_RETRIES) {
			k_msleep(DRV2605_I2C_RETRY_DELAY_MS);
		}
	}

	return err;
}

/**
 * @brief Read one byte from a DRV2605 register.
 * @param reg Register address to read.
 * @param value Output buffer for the register value.
 * @return 0 on success, or a negative error code on failure.
 */
static int drv2605_read_reg(uint8_t reg, uint8_t *value)
{
	return i2c_reg_read_byte(haptic_i2c, drv2605_addr, reg, value);
}

/**
 * @brief Check whether a DRV2605 responds at an I2C address.
 * @param addr I2C address to probe.
 * @return 0 if the status register was read, or a negative error code on failure.
 */
static int drv2605_probe_addr(uint16_t addr)
{
	uint8_t status;

	return i2c_reg_read_byte(haptic_i2c, addr, DRV2605_REG_STATUS, &status);
}

/**
 * @brief Initialize the haptic enable GPIO and the DRV2605 controller.
 * @return 0 on success, or a negative error code on failure.
 */
int drv2605_init(void)
{
	int err;

	if (drv2605_ready) {
		return 0;
	}

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

	err = i2c_recover_bus(haptic_i2c);
	if (err < 0 && err != -ENOSYS) {
		/* Keep the existing behavior: continue and try to probe the device. */
	}

	drv2605_addr = DRV2605_I2C_ADDR_LOW;
	err = drv2605_probe_addr(drv2605_addr);

	if (err < 0) {
		drv2605_addr = DRV2605_I2C_ADDR_HIGH;
		err = drv2605_probe_addr(drv2605_addr);
	}

	if (err < 0) {
		(void)gpio_pin_set_dt(&haptic_en, 0);
		return -ENODEV;
	}


	err = drv2605_write_reg(DRV2605_REG_RTP_INPUT, DRV2605_RTP_OFF);
    if (err < 0) {
        return err;
    }

	err = drv2605_write_reg(DRV2605_REG_MODE,
				DRV2605_MODE_STANDBY_INTERNAL_TRIGGER);
	if (err < 0) {
		return err;
	}

	drv2605_ready = true;

	return 0;
}

/**
 * @brief Set real-time playback mode and apply an amplitude.
 * @param amplitude Real-time playback amplitude to apply.
 * @return 0 on success, or a negative error code on failure.
 */
int drv2605_set_rtp(uint8_t amplitude)
{
	int err;

	err = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);
	if (err < 0) {
		return err;
	}

	return drv2605_write_reg(DRV2605_REG_RTP_INPUT, amplitude);
}

/**
 * @brief Stop real-time playback and return to internal trigger mode.
 * @return 0 on success, or the first negative error code on failure.
 */
int drv2605_stop(void)
{
	int rtp_err;
	int mode_err;

	rtp_err = drv2605_write_reg(DRV2605_REG_RTP_INPUT,
				    DRV2605_RTP_OFF);
	mode_err = drv2605_write_reg(DRV2605_REG_MODE,
				     DRV2605_MODE_STANDBY_INTERNAL_TRIGGER);

	if (rtp_err < 0) {
		return rtp_err;
	}

	return mode_err;
}

/**
 * @brief Check whether the DRV2605 was initialized successfully.
 * @return true if the controller is ready, otherwise false.
 */
bool drv2605_is_ready(void)
{
	return drv2605_ready;
}
