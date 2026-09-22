#ifndef DRV2605_H
#define DRV2605_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the DRV2605 controller.
 * @return 0 on success, or a negative error code on failure.
 */
int drv2605_init(void);

/**
 * @brief Set real-time playback mode and apply an amplitude.
 * @param amplitude Real-time playback amplitude to apply.
 * @return 0 on success, or a negative error code on failure.
 */
int drv2605_set_rtp(uint8_t amplitude);

/**
 * @brief Stop real-time playback and return to internal trigger mode.
 * @return 0 on success, or a negative error code on failure.
 */
int drv2605_stop(void);

/**
 * @brief Check whether the DRV2605 was initialized successfully.
 * @return true if the controller is ready, otherwise false.
 */
bool drv2605_is_ready(void);

#endif /* DRV2605_H */
