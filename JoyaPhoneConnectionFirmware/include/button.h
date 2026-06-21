#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "app_state.h"

extern struct k_msgq event_queue;

#define DEBOUNTE_TIME_MS 50
#define CLICK_WINDOW_MS 1000
#define BUTTON_ROUTINE_CANCEL_HOLD_MS 900
#define BUTTON_FACTORY_RESET_HOLD_MS 15000

int button_init(void);
void button_isr_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

#endif // BUTTON_H