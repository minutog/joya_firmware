#include "button.h"

#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(boton_emergencia), gpios);

static struct gpio_callback button_cb_data;

static struct k_work_delayable debounce_work;
static struct k_work_delayable click_window_work;
static struct k_work_delayable factory_reset_hold_work;

static void debounce_work_handler(struct k_work *work);
static void click_window_work_handler(struct k_work *work);
static void factory_reset_hold_work_handler(struct k_work *work);

static uint32_t press_start_ms;
static atomic_t pulse_count;

static bool last_stable_pressed = false;
static bool press_active = false;
static bool factory_reset_triggered = false;

static void send_button_event(event_type_t event)
{
	printk("Button event queued: %d\n", event);
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
		printk("Button read failed: %d\n", pressed_raw);
		return;
	}

	bool pressed = pressed_raw != 0;

	if (pressed == last_stable_pressed) {
		return;
	}

	last_stable_pressed = pressed;
	printk("Button stable state: pressed=%d\n", pressed);

	if (pressed) {
		press_start_ms = now_ms;
		press_active = true;
		factory_reset_triggered = false;
		(void)k_work_reschedule(&factory_reset_hold_work,
				 K_MSEC(BUTTON_FACTORY_RESET_HOLD_MS));

		if (atomic_get(&pulse_count) > 0) {
			(void)k_work_cancel_delayable(&click_window_work);
		}

		return;
	}

	(void)k_work_cancel_delayable(&factory_reset_hold_work);

	if (factory_reset_triggered) {
		printk("Button factory reset hold released\n");
		factory_reset_triggered = false;
		press_active = false;
		return;
	}

	if (!press_active) {
		return;
	}

	press_active = false;

	uint32_t press_time_ms = now_ms - press_start_ms;
	printk("Button released after %u ms\n", press_time_ms);

	if (press_time_ms >= BUTTON_ROUTINE_CANCEL_HOLD_MS) {
		(void)k_work_cancel_delayable(&click_window_work);
		atomic_set(&pulse_count, 0);

		if (press_time_ms >= BUTTON_FACTORY_RESET_HOLD_MS) {
			send_button_event(EV_BTN_FACTORY_RESET);
		} else {
			send_button_event(EV_BTN_LONG_PRESS);
		}

		return;
	}


	atomic_inc(&pulse_count);

	(void)k_work_reschedule(&click_window_work,
				 K_MSEC(CLICK_WINDOW_MS));
}

static void click_window_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint32_t count = atomic_set(&pulse_count, 0);

	if (count == 0) {
		return;
	}

	if (count == 1) {
		printk("Button gesture: single click\n");
		send_button_event(EV_BTN_1_PULSE);
	} else if (count == 2) {
		printk("Button gesture: double click\n");
		send_button_event(EV_BTN_2_PULSE);
	} else {
		printk("Button gesture: emergency (%u clicks)\n", count);
		send_button_event(EV_BTN_EMERGENCY);
	}
}

static void factory_reset_hold_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int pressed_raw = gpio_pin_get_dt(&button);
	if (pressed_raw <= 0) {
		return;
	}

	factory_reset_triggered = true;
	press_active = false;
	atomic_set(&pulse_count, 0);
	(void)k_work_cancel_delayable(&click_window_work);

	printk("Button gesture: factory reset hold (%u ms)\n",
	       BUTTON_FACTORY_RESET_HOLD_MS);
	send_button_event(EV_BTN_FACTORY_RESET);
}

int button_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		printk("Button GPIO not ready\n");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Button configure failed: %d\n", ret);
		return ret;
	}

	int initial_pressed = gpio_pin_get_dt(&button);
	if (initial_pressed < 0) {
		printk("Button initial read failed: %d\n", initial_pressed);
		return initial_pressed;
	}

	last_stable_pressed = initial_pressed != 0;
	press_active = false;
	atomic_set(&pulse_count, 0);
	printk("Button init: port=%s pin=%u initial_pressed=%d\n",
	       button.port->name, button.pin, last_stable_pressed);

	k_work_init_delayable(&debounce_work, debounce_work_handler);
	k_work_init_delayable(&click_window_work, click_window_work_handler);
	k_work_init_delayable(&factory_reset_hold_work, factory_reset_hold_work_handler);

	gpio_init_callback(&button_cb_data,
			   button_isr_handler,
			   BIT(button.pin));

	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret != 0) {
		printk("Button callback add failed: %d\n", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		(void)gpio_remove_callback(button.port, &button_cb_data);
		printk("Button interrupt configure failed: %d\n", ret);
		return ret;
	}

	return 0;
}
