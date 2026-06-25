#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include "app_state.h"

extern struct k_msgq event_queue;

#define DEBOUNTE_TIME_MS 50
#define CLICK_WINDOW_MS 900
#define BUTTON_ROUTINE_CANCEL_HOLD_MS 500
#define BUTTON_FACTORY_RESET_HOLD_MS 15000

/**
 * @brief Initialize the button GPIO and interrupt handling.
 * @return 0 on success, or a negative error code on failure.
 */
int button_init(void);

/**
 * @brief Handle button GPIO interrupts.
 * @param dev GPIO device that generated the interrupt.
 * @param cb GPIO callback data.
 * @param pins GPIO pins that generated the interrupt.
 */
void button_isr_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

#endif // BUTTON_H
