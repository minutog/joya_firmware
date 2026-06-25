#include "button.h"

#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(button_mod, LOG_LEVEL_NONE);

static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(boton_emergencia), gpios);

static struct gpio_callback button_cb_data;

static struct k_work_delayable debounce_work;
static struct k_work_delayable click_window_work;

static void debounce_work_handler(struct k_work *work);
static void click_window_work_handler(struct k_work *work);

static uint32_t press_start_ms;
static atomic_t pulse_count;

static bool last_stable_pressed = false;
static bool press_active = false;

static void send_button_event(event_type_t event)
{
	add_event(event);
}

void button_isr_handler(const struct device *dev,
			struct gpio_callback *cb,
			uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	(void)k_work_reschedule(&debounce_work, K_MSEC(DEBOUNTE_TIME_MS));
}

static void debounce_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint32_t now_ms = k_uptime_get_32();

	int pressed_raw = gpio_pin_get_dt(&button);
	if (pressed_raw < 0) {
		LOG_WRN("BTN read failed: %d", pressed_raw);
		return;
	}

	bool pressed = pressed_raw != 0;

	if (pressed == last_stable_pressed) {
		LOG_INF("BTN stable unchanged: pressed=%d", pressed);
		return;
	}

	last_stable_pressed = pressed;

	LOG_INF("BTN stable edge: pressed=%d now=%u pulse_count=%d pending=%d",
		pressed,
		now_ms,
		atomic_get(&pulse_count),
		k_work_delayable_is_pending(&click_window_work));

	if (pressed) {
		press_start_ms = now_ms;
		press_active = true;

		if (atomic_get(&pulse_count) > 0) {
			LOG_INF("BTN next press: cancel click window");
			(void)k_work_cancel_delayable(&click_window_work);
		}

		return;
	}

	if (!press_active) {
		LOG_WRN("BTN release ignored: no active press");
		return;
	}

	press_active = false;

	uint32_t press_time_ms = now_ms - press_start_ms;

	LOG_INF("BTN release: press_time=%u ms", press_time_ms);

	if (press_time_ms >= BUTTON_ROUTINE_CANCEL_HOLD_MS) {
		(void)k_work_cancel_delayable(&click_window_work);
		atomic_set(&pulse_count, 0);

		if (press_time_ms >= BUTTON_FACTORY_RESET_HOLD_MS) {
			LOG_INF("BTN event: factory reset");
			send_button_event(EV_BTN_FACTORY_RESET);
		} else {
			LOG_INF("BTN event: long press");
			send_button_event(EV_BTN_LONG_PRESS);
		}

		return;
	}


	atomic_inc(&pulse_count);

	LOG_INF("BTN short pulse counted: pulse_count=%d",
		atomic_get(&pulse_count));

	(void)k_work_reschedule(&click_window_work,
				 K_MSEC(CLICK_WINDOW_MS));

	LOG_INF("BTN click window scheduled: %u ms", CLICK_WINDOW_MS);
}

static void click_window_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint32_t count = atomic_set(&pulse_count, 0);

	LOG_INF("BTN window expired: count=%u", count);

	if (count == 0) {
		return;
	}

	if (count == 1) {
		LOG_INF("BTN event: 1 pulse");
		send_button_event(EV_BTN_1_PULSE);
	} else if (count == 2) {
		LOG_INF("BTN event: 2 pulses");
		send_button_event(EV_BTN_2_PULSE);
	} else {
		LOG_INF("BTN event: emergency pulses");
		send_button_event(EV_BTN_EMERGENCY);
	}
}

int button_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Button GPIO is not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Failed to configure button GPIO: %d", ret);
		return ret;
	}

	int initial_pressed = gpio_pin_get_dt(&button);
	if (initial_pressed < 0) {
		LOG_ERR("Failed to read initial button state: %d", initial_pressed);
		return initial_pressed;
	}

	last_stable_pressed = initial_pressed != 0;
	press_active = false;
	atomic_set(&pulse_count, 0);

	k_work_init_delayable(&debounce_work, debounce_work_handler);
	k_work_init_delayable(&click_window_work, click_window_work_handler);

	gpio_init_callback(&button_cb_data,
			   button_isr_handler,
			   BIT(button.pin));

	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret != 0) {
		LOG_ERR("Failed to add button callback: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		LOG_ERR("Failed to configure button interrupt: %d", ret);
		(void)gpio_remove_callback(button.port, &button_cb_data);
		return ret;
	}

	LOG_INF("Button initialized. initial_pressed=%d", last_stable_pressed);

	return 0;
}