#ifndef HAPTICS_H
#define HAPTICS_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

enum haptics_pattern {
	HAPTICS_PATTERN_NONE = 0,
	HAPTICS_PATTERN_SETUP_MODE,
	HAPTICS_PATTERN_ROUTINE_START,
	HAPTICS_PATTERN_ROUTINE_CANCEL,
	HAPTICS_PATTERN_EMERGENCY_START,
	HAPTICS_PATTERN_FOLLOW_ME,
    HAPTICS_PATTERN_FRIEND_EMERGENCY,
	//HAPTICS_PATTERN_DIAGNOSTIC,
};

enum haptics_effect {
    HAPTICS_EFFECT_AUTH = 0x01,
    HAPTICS_EFFECT_RESET = 0x2F,
};

int haptics_init(void);
void haptics_play(enum haptics_pattern pattern);
void haptics_stop(void);
bool haptics_is_ready(void);
void haptics_play_effect(enum haptics_effect effect);

#endif /* HAPTICS_H */