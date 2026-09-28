#include "haptics.h"

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

static atomic_t haptics_available = ATOMIC_INIT(0);

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
/*
static const struct haptic_step pattern_setup_mode[] = {
	{ 80,  55 },
	{ 80,  75 },
	{ 80,  95 },
	{ 120, 115 },
	{ 180, 0   },
};
*/
// struct haptic_step = [duration_ms, amplitude]
static const struct haptic_step pattern_setup_mode[] = {
	{ 700,  50 },
	{ 100, 0 },
};

static const struct haptic_step pattern_ack_connection[] = {
	{ 200, 50},
	{ 100, 0},
	{ 200, 50},
	{ 100, 0},
	{ 200, 50},
	{ 100, 0},
};

static const struct haptic_step pattern_routine_start[] = {
	{ 700,  50 },
	{ 100,  0   },
};

static const struct haptic_step pattern_routine_cancel[] = {
	{ 200, 50 },
	{ 100, 0  },
	{ 200, 50  },
	{ 100, 0   },
};

static const struct haptic_step pattern_emergency_start[] = {
	{ 2000, 50 },
	{ 100, 0 },
};

static const struct haptic_step pattern_follow_me[] = {
	{ 200, 50  },
	{ 100, 0   },
	{ 200, 50 },
	{ 100, 0   },
};

static const struct haptic_step pattern_friend_emergency[] = {
    { 200, 50 }, 
	{ 100, 0 },
    { 200, 50 }, 
	{ 350, 0 },
    { 200, 50 }, 
	{ 100, 0 },
    { 200, 50 }, 
	{ 350, 0 },
	{ 200, 50 }, 
	{ 100, 0 },
    { 200, 50 }, 
	{ 100, 0 },
};

static const struct haptic_step pattern_factory_reset[] = {
	{ 200, 50},
	{ 100, 0},
	{ 200, 50},
	{ 100, 0},
	{ 200, 50},
	{ 100, 0},
};

/**
 * @brief Write one byte to a DRV2605 register.
 * @param reg Register address to write.
 * @param value Value to write into the register.
 * @return 0 on success, or a negative error code on failure.
 */
static int drv2605_write_reg(uint8_t reg, uint8_t value)
{
	// Create a buffer containing the register address followed by the value to write
	uint8_t data[2] = { reg, value };

	return i2c_write(haptic_i2c, data, sizeof(data), drv2605_addr);
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
 * @brief Set the DRV2605 to real-time playback mode and apply an amplitude.
 * @param amplitude Real-time playback amplitude to apply.
 * @return 0 on success, or a negative error code on failure.
 */
static int drv2605_set_rtp(uint8_t amplitude)
{
	int err;

	// Set the DRV2605 to real-time playback mode
	err = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);
	if (err < 0) {
		return err;
	}

	// Write the amplitude to the RTP_INPUT register
	err = drv2605_write_reg(DRV2605_REG_RTP_INPUT, amplitude);
	if (err < 0) {
		return err;
	}

	return 0;
}

/**
 * @brief Stop real-time playback and reset the active pattern state.
 */
static void drv2605_idle(void)
{
	// Set the DRV2605 to internal trigger mode and turn off real-time playback
	(void)drv2605_write_reg(DRV2605_REG_RTP_INPUT, HAPTIC_RTP_OFF);
	(void)drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL_TRIGGER);

	int err = nbm5100_set_active(false);
    if (err < 0) {
        // TODO: decide fallback strategy
    }

	// Reset the active pattern state
	active_pattern = HAPTICS_PATTERN_NONE;
	active_step_index = 0;
}


/**
 * @brief Get the step sequence associated with a haptic pattern.
 * @param pattern Pattern whose sequence is requested.
 * @param step_count Output for the number of steps in the sequence.
 * @return Pointer to the step sequence, or NULL if the pattern is invalid or empty.
 */
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

	case HAPTICS_PATTERN_ACK_CONNECTION:
		*step_count = ARRAY_SIZE(pattern_ack_connection);
		return pattern_ack_connection;

	case HAPTICS_PATTERN_FACTORY_RESET:
		*step_count = ARRAY_SIZE(pattern_factory_reset);
		return pattern_factory_reset;

	case HAPTICS_PATTERN_NONE:
	default:
		*step_count = 0;
		return NULL;
	}
}

/**
 * @brief Retrieve and advance to the next step of the active pattern.
 * @param amplitude Output for the amplitude of the next step.
 * @param duration_ms Output for the duration of the next step, in milliseconds.
 * @return true if a step was returned, or false if the pattern is complete.
 */
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

/**
 * @brief Execute one step of the active pattern and schedule the following step.
 * @param work Work item associated with the haptic pattern scheduler.
 */
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

	err = drv2605_set_rtp(amplitude);
	if (err < 0) {
		drv2605_idle();
		return;
	}

	(void)k_work_reschedule(&haptics_pattern_work, K_MSEC(duration_ms));
}

/**
 * PUBLIC API
 */

/**
 * @brief Initialize the haptic enable GPIO and the DRV2605 controller.
 * @return 0 on success, or a negative error code on failure.
 */
int haptics_init(void)
{
	int err;
	uint8_t status = 0;

if (haptics_ready) {
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
		// (improvement): decide what to do if I2C bus recovery fails (e.g., retry, log error, etc.)
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
	
	(void)drv2605_read_reg(DRV2605_REG_STATUS, &status);
	
	err = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL_TRIGGER);
	if (err < 0) {
		return err;
	}
	
	// Note: 0x01 is the value used in the old firmware
	err = drv2605_write_reg(DRV2605_REG_LIBRARY, 0x01);
	if (err < 0) {
		return err;
	}

	haptics_ready = true;
	
	return 0;
}

/**
 * @brief Start a haptic pattern from its first step.
 * @param pattern Pattern to play, or HAPTICS_PATTERN_NONE to stop playback.
 */
void haptics_play(enum haptics_pattern pattern)
{
	if (!haptics_ready) {
		// (improvement): decide what to do if haptics is not initialized (e.g., log error, return error, etc.)
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

/**
 * @brief Cancel the active pattern and place the DRV2605 in its idle state.
 */
void haptics_stop(void)
{
	(void)k_work_cancel_delayable(&haptics_pattern_work);

	if (haptics_ready) {
		drv2605_idle();
	}
}

/**
 * @brief Check whether the haptic controller was initialized successfully.
 * @return true if the controller is ready, otherwise false.
 */
bool haptics_is_ready(void)
{
	return haptics_ready;
}

void haptics_set_available(bool available)
{
    atomic_set(&haptics_available, available ? 1 : 0);
}

bool haptics_are_available(void)
{
    return atomic_get(&haptics_available) != 0;
}