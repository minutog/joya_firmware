#include "haptics_test_indicator.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define TEST_LED_NODE DT_ALIAS(led0)

#if !DT_NODE_EXISTS(TEST_LED_NODE) || !DT_NODE_HAS_STATUS(TEST_LED_NODE, okay)
#error "Haptic diagnostic requires an enabled led0 alias"
#endif

static const struct gpio_dt_spec test_led = GPIO_DT_SPEC_GET(TEST_LED_NODE, gpios);
static bool test_led_ready;

static void test_led_off(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)gpio_pin_set_dt(&test_led, 0);
}

static K_WORK_DELAYABLE_DEFINE(test_led_off_work, test_led_off);

void haptics_test_indicator_request(bool battery_allows)
{
	if (!battery_allows) {
		return;
	}

	if (!test_led_ready) {
		if (!gpio_is_ready_dt(&test_led) ||
		    gpio_pin_configure_dt(&test_led, GPIO_OUTPUT_INACTIVE) != 0) {
			return;
		}
		test_led_ready = true;
	}

	if (gpio_pin_set_dt(&test_led, 1) == 0) {
		if (k_work_reschedule(&test_led_off_work, K_MSEC(200)) < 0) {
			(void)gpio_pin_set_dt(&test_led, 0);
		}
	}
}
