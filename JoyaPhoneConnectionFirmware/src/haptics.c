#include "haptics.h"
#include "drv2605.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define HAPTIC_RTP_INTENSITY 		122 //antes: 50

/* ============================================================
 * Internal state
 * ============================================================ */

static atomic_t haptics_available = ATOMIC_INIT(0);

struct haptic_step {
	uint16_t duration_ms;
	uint8_t amplitude;
};

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
	{ 700,  HAPTIC_RTP_INTENSITY},
	{ 100, 0 },
};

static const struct haptic_step pattern_ack_connection[] = {
	{ 200,  HAPTIC_RTP_INTENSITY},
	{ 100, 0},
	{ 200,  HAPTIC_RTP_INTENSITY},
	{ 100, 0},
	{ 200,  HAPTIC_RTP_INTENSITY},
	{ 100, 0},
};

static const struct haptic_step pattern_routine_start[] = {
	{ 700,  HAPTIC_RTP_INTENSITY },
	{ 100,  0   },
};

static const struct haptic_step pattern_routine_cancel[] = {
	{ 200,  HAPTIC_RTP_INTENSITY },
	{ 100, 0  },
	{ 200,  HAPTIC_RTP_INTENSITY  },
	{ 100, 0   },
};

static const struct haptic_step pattern_emergency_start[] = {
	{ 2000,  HAPTIC_RTP_INTENSITY },
	{ 100, 0 },
};

static const struct haptic_step pattern_follow_me[] = {
	{ 200,  HAPTIC_RTP_INTENSITY  },
	{ 100, 0   },
	{ 200,  HAPTIC_RTP_INTENSITY },
	{ 100, 0   },
};

static const struct haptic_step pattern_friend_emergency[] = {
    { 200,  HAPTIC_RTP_INTENSITY }, 
	{ 100, 0 },
    { 200,  HAPTIC_RTP_INTENSITY }, 
	{ 350, 0 },
    { 200,  HAPTIC_RTP_INTENSITY }, 
	{ 100, 0 },
    { 200,  HAPTIC_RTP_INTENSITY }, 
	{ 350, 0 },
	{ 200,  HAPTIC_RTP_INTENSITY }, 
	{ 100, 0 },
    { 200,  HAPTIC_RTP_INTENSITY }, 
	{ 100, 0 },
};

static const struct haptic_step pattern_factory_reset[] = {
	{ 200,  HAPTIC_RTP_INTENSITY},
	{ 100, 0},
	{ 200,  HAPTIC_RTP_INTENSITY},
	{ 100, 0},
	{ 200,  HAPTIC_RTP_INTENSITY},
	{ 100, 0},
};

/**
 * @brief Stop haptic playback and reset pattern state.
 * @return 0 on success, or a negative error code if cleanup was incomplete.
 */
static int haptics_idle(void)
{
	int drv_err;

	drv_err = drv2605_stop();

	/*
	 * Always release the software state. A runtime I2C failure aborts only
	 * the current vibration; a later request is allowed to try again.
	 */
	active_pattern = HAPTICS_PATTERN_NONE;
	active_step_index = 0;

	return drv_err;
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
		(void)haptics_idle();
		return;
	}

	err = drv2605_set_rtp(amplitude);
	if (err < 0) {
		(void)haptics_idle();
		return;
	}

	(void)k_work_reschedule(&haptics_pattern_work, K_MSEC(duration_ms));
}

/**
 * @brief Start a pattern through the DRV2605.
 * @param pattern Pattern to start.
 */
static void haptics_start_pattern(enum haptics_pattern pattern)
{
	if (!drv2605_is_ready()) {
		return;
	}

	active_pattern = pattern;
	active_step_index = 0;

	(void)k_work_schedule(&haptics_pattern_work, K_NO_WAIT);
}

/**
 * @brief Cancel the active pattern scheduler and wait for its handler to finish.
 *
 * This stops only the scheduler. It does not wait for the complete pattern and
 * does not place the hardware in idle mode.
 */
static void haptics_cancel_pattern_work(void)
{
	struct k_work_sync sync;

	(void)k_work_cancel_delayable_sync(&haptics_pattern_work, &sync);
}

/**
 * PUBLIC API
 */

/**
 * @brief Initialize the haptic controller.
 * @return 0 on success, or a negative error code on failure.
 */
int haptics_init(void)
{
	return drv2605_init();
}

/**
 * @brief Request a haptic pattern for immediate playback through the DRV2605.
 * @param pattern Pattern to play, or HAPTICS_PATTERN_NONE to stop playback.
 */
void haptics_play_when_ready(enum haptics_pattern pattern)
{
	if (pattern == HAPTICS_PATTERN_NONE) {
		haptics_stop();
		return;
	}

	if (!haptics_are_available()) {
		return;
	}

	/*
	 * A new request preempts the previous one. Synchronous cancellation makes
	 * sure an old handler cannot later modify or clear the new pattern state.
	 */
	haptics_cancel_pattern_work();
	haptics_start_pattern(pattern);
}

/**
 * @brief Cancel the active pattern and place the DRV2605 in its idle state.
 */
void haptics_stop(void)
{
	haptics_cancel_pattern_work();

	if (drv2605_is_ready()) {
		(void)haptics_idle();
	}
}

/**
 * @brief Check whether the haptic controller was initialized successfully.
 * @return true if the controller is ready, otherwise false.
 */
bool haptics_is_ready(void)
{
	return drv2605_is_ready();
}

void haptics_set_available(bool available)
{
    atomic_set(&haptics_available, available ? 1 : 0);
}

bool haptics_are_available(void)
{
    return atomic_get(&haptics_available) != 0;
}
