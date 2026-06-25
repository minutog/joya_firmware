#include "haptics.h"

LOG_MODULE_REGISTER(haptics, LOG_LEVEL_INF);

#define HAPTIC_NODE DT_PATH(zephyr_user)

#if !DT_NODE_EXISTS(HAPTIC_NODE)
#error "Missing /zephyr,user node in overlay"
#endif

#if !DT_NODE_HAS_PROP(HAPTIC_NODE, haptic_en_gpios)
#error "Missing haptic_en_gpios property in /zephyr,user"
#endif

static const struct gpio_dt_spec haptic_en =
	GPIO_DT_SPEC_GET(HAPTIC_NODE, haptic_en_gpios);

static const struct device *haptic_i2c =
	DEVICE_DT_GET(DT_NODELABEL(i2c0));


/* ============================================================
 * Internal state
 * ============================================================ */

struct haptic_step {
	uint16_t duration_ms;
	uint8_t amplitude;
};

static uint16_t drv2605_addr;
static bool haptics_ready;

static enum haptics_pattern active_pattern = HAPTICS_PATTERN_NONE;
static size_t active_step_index;

static void haptics_pattern_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(haptics_pattern_work, haptics_pattern_work_handler);

static const struct haptic_step pattern_setup_mode[] = {
	{ 80,  55 },
	{ 80,  75 },
	{ 80,  95 },
	{ 120, 115 },
	{ 180, 0   },
};

static const struct haptic_step pattern_routine_start[] = {
	{ 90,  104 },
	{ 70,  0   },
	{ 90,  104 },
	{ 180, 0   },
	{ 110, 108 },
	{ 160, 0   },
	{ 320, 120 },
	{ 120, 0   },
};

static const struct haptic_step pattern_routine_cancel[] = {
	{ 180, 115 },
	{ 120, 90  },
	{ 120, 65  },
	{ 240, 0   },
};

static const struct haptic_step pattern_emergency_start[] = {
	{ 110, 104 },
	{ 100, 0   },
	{ 150, 118 },
	{ 260, 0   },
	{ 110, 104 },
	{ 100, 0   },
	{ 150, 118 },
	{ 260, 0   },
};

static const struct haptic_step pattern_follow_me[] = {
	{ 420, 92  },
	{ 140, 0   },
	{ 110, 112 },
	{ 100, 0   },
	{ 110, 112 },
	{ 420, 0   },
};

static const struct haptic_step pattern_friend_emergency[] = {
    { 180, 120 }, { 120, 0 },
    { 180, 120 }, { 120, 0 },
    { 600, 127 }, { 400, 0 },
};

static int drv2605_write_reg(uint8_t reg, uint8_t value)
{
	uint8_t data[2] = { reg, value };

	return i2c_write(haptic_i2c, data, sizeof(data), drv2605_addr);
}

static int drv2605_read_reg(uint8_t reg, uint8_t *value)
{
	return i2c_reg_read_byte(haptic_i2c, drv2605_addr, reg, value);
}

static int drv2605_probe_addr(uint16_t addr)
{
	uint8_t status;

	return i2c_reg_read_byte(haptic_i2c, addr, DRV2605_REG_STATUS, &status);
}

static int drv2605_set_rtp(uint8_t amplitude)
{
	int err;

	err = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);
	if (err < 0) {
		LOG_ERR("Failed to set RTP mode: %d", err);
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_RTP_INPUT, amplitude);
	if (err < 0) {
		LOG_ERR("Failed to write RTP input: %d", err);
		return err;
	}

	return 0;
}

static void drv2605_idle(void)
{
	(void)drv2605_write_reg(DRV2605_REG_RTP_INPUT, HAPTIC_RTP_OFF);
	(void)drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL_TRIGGER);

	active_pattern = HAPTICS_PATTERN_NONE;
	active_step_index = 0;
}

static int drv2605_play_effect(uint8_t effect)
{
	int err;

	err = drv2605_write_reg(DRV2605_REG_WAVESEQ1, effect);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_WAVESEQ2, 0x00);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL_TRIGGER);
	if (err < 0) {
		return err;
	}

	err = drv2605_write_reg(DRV2605_REG_GO, 0x01);
	if (err < 0) {
		return err;
	}

	return 0;
}

static const struct haptic_step *get_pattern_steps(enum haptics_pattern pattern,
						   size_t *step_count)
{
	switch (pattern) {
	case HAPTICS_PATTERN_SETUP_MODE:
		*step_count = ARRAY_SIZE(pattern_setup_mode);
		return pattern_setup_mode;

	case HAPTICS_PATTERN_ROUTINE_START:
		*step_count = ARRAY_SIZE(pattern_routine_start);
		return pattern_routine_start;

	case HAPTICS_PATTERN_ROUTINE_CANCEL:
		*step_count = ARRAY_SIZE(pattern_routine_cancel);
		return pattern_routine_cancel;

	case HAPTICS_PATTERN_EMERGENCY_START:
		*step_count = ARRAY_SIZE(pattern_emergency_start);
		return pattern_emergency_start;

	case HAPTICS_PATTERN_FOLLOW_ME:
		*step_count = ARRAY_SIZE(pattern_follow_me);
		return pattern_follow_me;

	case HAPTICS_PATTERN_FRIEND_EMERGENCY:
		*step_count = ARRAY_SIZE(pattern_friend_emergency);
		return pattern_friend_emergency;

	case HAPTICS_PATTERN_NONE:
	default:
		*step_count = 0;
		return NULL;
	}
}

static bool get_next_step(uint8_t *amplitude, uint16_t *duration_ms)
{
	const struct haptic_step *steps;
	size_t step_count;

	steps = get_pattern_steps(active_pattern, &step_count);
	if (steps == NULL || active_step_index >= step_count) {
		return false;
	}

	*duration_ms = steps[active_step_index].duration_ms;
	*amplitude = steps[active_step_index].amplitude;

	active_step_index++;

	return true;
}

static void haptics_pattern_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint8_t amplitude;
	uint16_t duration_ms;
	int err;

	if (!get_next_step(&amplitude, &duration_ms)) {
		drv2605_idle();
		return;
	}

	LOG_DBG("Haptic step: pattern=%d amplitude=%u duration=%u ms",
		active_pattern, amplitude, duration_ms);

	err = drv2605_set_rtp(amplitude);
	if (err < 0) {
		LOG_ERR("Stopping haptic pattern due to I2C error: %d", err);
		drv2605_idle();
		return;
	}

	(void)k_work_reschedule(&haptics_pattern_work, K_MSEC(duration_ms));
}

/**
 * PUBLIC API
 */

int haptics_init(void)
{
	int err;
	uint8_t status = 0;

	if (haptics_ready) {
		return 0;
	}

	if (!device_is_ready(haptic_i2c)) {
		LOG_WRN("I2C device is not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&haptic_en)) {
		LOG_WRN("Haptic enable GPIO is not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&haptic_en, GPIO_OUTPUT_ACTIVE);
	if (err < 0) {
		LOG_ERR("Failed to configure haptic enable GPIO: %d", err);
		return err;
	}

	k_msleep(10);

	err = i2c_recover_bus(haptic_i2c);
	if (err < 0 && err != -ENOSYS) {
		LOG_WRN("I2C bus recover failed: %d", err);
	}

	drv2605_addr = DRV2605_I2C_ADDR_LOW;
	err = drv2605_probe_addr(drv2605_addr);

	if (err < 0) {
		LOG_WRN("DRV2605 not found at 0x%02x, trying 0x%02x",
			DRV2605_I2C_ADDR_LOW, DRV2605_I2C_ADDR_HIGH);

		drv2605_addr = DRV2605_I2C_ADDR_HIGH;
		err = drv2605_probe_addr(drv2605_addr);
	}

	if (err < 0) {
		LOG_ERR("DRV2605 not found on I2C bus");
		(void)gpio_pin_set_dt(&haptic_en, 0);
		return -ENODEV;
	}

	(void)drv2605_read_reg(DRV2605_REG_STATUS, &status);
	LOG_INF("DRV2605 found at 0x%02x, status=0x%02x", drv2605_addr, status);

	err = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL_TRIGGER);
	if (err < 0) {
		LOG_ERR("Failed to set internal trigger mode: %d", err);
		return err;
	}

	// Note: 0x01 is the value used in the old firmware
	err = drv2605_write_reg(DRV2605_REG_LIBRARY, 0x01);
	if (err < 0) {
		LOG_ERR("Failed to set DRV2605 library: %d", err);
		return err;
	}

	haptics_ready = true;

	LOG_INF("Haptics initialized");

	return 0;
}

void haptics_play_effect(enum haptics_effect effect)
{
    if (!haptics_ready) {
        LOG_WRN("Haptics not initialized");
        return;
    }

    (void)k_work_cancel_delayable(&haptics_pattern_work);

    active_pattern = HAPTICS_PATTERN_NONE;
    active_step_index = 0;

    int err = drv2605_play_effect((uint8_t)effect);
    if (err < 0) {
        LOG_ERR("Failed to play effect %d (%d)", effect, err);
    }
}

void haptics_play(enum haptics_pattern pattern)
{
	if (!haptics_ready) {
        LOG_WRN("Haptics not initialized");
        return;
    }
	
	if (pattern == HAPTICS_PATTERN_NONE) {
		haptics_stop();
		return;
	}

	(void)k_work_cancel_delayable(&haptics_pattern_work);

	active_pattern = pattern;
	active_step_index = 0;

	(void)k_work_schedule(&haptics_pattern_work, K_NO_WAIT);
}

void haptics_stop(void)
{
	(void)k_work_cancel_delayable(&haptics_pattern_work);

	if (haptics_ready) {
		drv2605_idle();
	}
}

bool haptics_is_ready(void)
{
	return haptics_ready;
}