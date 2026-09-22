#ifndef HAPTICS_H
#define HAPTICS_H

#include <stdbool.h>

enum haptics_pattern {
	HAPTICS_PATTERN_NONE = 0,
	HAPTICS_PATTERN_SETUP_MODE,
	HAPTICS_PATTERN_ROUTINE_START,
	HAPTICS_PATTERN_ROUTINE_CANCEL,
	HAPTICS_PATTERN_EMERGENCY_START,
	HAPTICS_PATTERN_FOLLOW_ME,
	HAPTICS_PATTERN_FRIEND_EMERGENCY,
	HAPTICS_PATTERN_FACTORY_RESET,
	HAPTICS_PATTERN_ACK_CONNECTION,
};

/**
 * @brief Initialize the haptic subsystem.
 * @return 0 on success, or a negative error code on failure.
 */
int haptics_init(void);

/**
 * @brief Request a haptic pattern after the NBM reports ready.
 * @param pattern Pattern to play, or HAPTICS_PATTERN_NONE to stop playback.
 * @note This function must be called from thread context.
 */
void haptics_play_when_ready(enum haptics_pattern pattern);

/**
 * @brief Stop the active haptic pattern.
 * @note This function must be called from thread context.
 */
void haptics_stop(void);

/**
 * @brief Check whether the DRV2605 was initialized successfully.
 * @return true if the controller is ready, otherwise false.
 */
bool haptics_is_ready(void);

/**
 * @brief Set whether the complete haptic subsystem is available.
 * @param available true when the NBM and DRV2605 can be used.
 */
void haptics_set_available(bool available);

/**
 * @brief Check whether the complete haptic subsystem is available.
 * @return true if haptic playback is available, otherwise false.
 */
bool haptics_are_available(void);

#endif /* HAPTICS_H */
