#ifndef HAPTICS_H
#define HAPTICS_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#define DRV2605_I2C_ADDR_LOW			0x5A
#define DRV2605_I2C_ADDR_HIGH			0x5B

#define DRV2605_REG_STATUS				0x00
#define DRV2605_REG_MODE				0x01
#define DRV2605_REG_RTP_INPUT			0x02
#define DRV2605_REG_LIBRARY				0x03
#define DRV2605_REG_WAVESEQ1			0x04
#define DRV2605_REG_WAVESEQ2			0x05
#define DRV2605_REG_GO					0x0C

#define DRV2605_MODE_INTERNAL_TRIGGER	0x00
#define DRV2605_MODE_RTP				0x05

#define HAPTIC_RTP_OFF					0
#define HAPTIC_RTP_MAX					127

enum haptics_pattern {
	HAPTICS_PATTERN_NONE = 0,
	HAPTICS_PATTERN_SETUP_MODE,
	HAPTICS_PATTERN_ROUTINE_START,
	HAPTICS_PATTERN_ROUTINE_CANCEL,
	HAPTICS_PATTERN_EMERGENCY_START,
	HAPTICS_PATTERN_FOLLOW_ME,
    HAPTICS_PATTERN_FRIEND_EMERGENCY,
};

enum haptics_effect {
    HAPTICS_EFFECT_AUTH = 0x01,
    HAPTICS_EFFECT_RESET = 0x2F,
};

/**
 * @brief Initialize the haptic driver.
 * @return 0 on success, or a negative error code on failure.
 */
int haptics_init(void);

/**
 * @brief Stop the active haptic pattern or effect.
 */
void haptics_stop(void);

/**
 * @brief Check whether the haptic driver is ready.
 * @return true if the haptic driver is ready, false otherwise.
 */
bool haptics_is_ready(void);

/**
 * @brief Play a haptic pattern.
 * @param pattern The haptic pattern to play.
 */
void haptics_play(enum haptics_pattern pattern);

/**
 * @brief Play one haptic effect.
 * @param effect The haptic effect to play.
 */
void haptics_play_effect(enum haptics_effect effect);

#endif /* HAPTICS_H */