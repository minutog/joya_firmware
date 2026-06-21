#include "button.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button_mod, LOG_LEVEL_INF);

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(boton_emergencia), gpios);
static struct gpio_callback button_cb_data;

static struct k_work_delayable click_window_work;
static void click_window_work_handler(struct k_work *work);

static struct k_work button_work;
static void button_work_handler(struct k_work *work);

volatile struct button_event {
    uint32_t timestamp;
    int state;
} last_event = {0, 0};

static uint32_t last_edge_ms = 0;
static volatile uint32_t pulse_count = 0;

int button_init(void) {
    int ret;

    // 1. Verificar si el hardware existe
    if (!gpio_is_ready_dt(&button)) {
        return -ENODEV;
    }

    // 2. Configurar el pin como entrada con interrupción (Flanco BAJADA y SUBIDA)
    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0) {
        return ret;
    }
    
    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
    if (ret != 0) {
        return ret;
    }

    k_work_init(&button_work, button_work_handler);
    k_work_init_delayable(&click_window_work, click_window_work_handler);
    gpio_init_callback(&button_cb_data, button_isr_handler, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    return 0;
}

static void button_work_handler(struct k_work *work) {
    if(last_event.state == 1){ // Rising edge
        last_edge_ms = last_event.timestamp;

        k_work_reschedule(&click_window_work, K_MSEC(CLICK_WINDOW_MS));
        

    } else { // Falling edge
        uint32_t press_time = k_uptime_get_32() - last_edge_ms;

        if(press_time >= BUTTON_ROUTINE_CANCEL_HOLD_MS){
            k_work_cancel_delayable(&click_window_work);
            pulse_count = 0;
            event_type_t event_to_send;

            if(press_time >= BUTTON_FACTORY_RESET_HOLD_MS){
                // Handle factory reset
                event_to_send = EV_BTN_FACTORY_RESET;
            } else {
                // Handle routine cancel
                event_to_send = EV_BTN_LONG_PRESS;
            }

            k_msgq_put(&event_queue, &event_to_send, K_NO_WAIT);
            return;
        }

        if(k_work_delayable_is_pending(&click_window_work)){
            pulse_count++;
        } 
        
        // Note: if the click window work finished before this falling edge, we won't count this pulse. This is intentional to avoid counting long presses as multiple clicks. The pulse_count will be reset in the click_window_work handler.
    }
}

void button_isr_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
    uint32_t last_edge_ms_tmp = k_uptime_get_32();

    // Debounce filter (for both rising and falling edges)
    if((last_edge_ms_tmp - last_edge_ms) < DEBOUNTE_TIME_MS){
        return;
    }

    // Now that we have a valid edge, we can update the last_edge_ms variable
    last_event.timestamp = last_edge_ms_tmp;
    last_event.state = gpio_pin_get_dt(&button);

    k_work_submit(&button_work);
}

// This function is called after the click window expires. We can now determine how many clicks were detected.
static void click_window_work_handler(struct k_work *work) {
    event_type_t event_to_send;
    int count = pulse_count;
    pulse_count = 0;
    //LOG_INF("Ventana cerrada. Clics contados: %d", count);

    if(count == 1){
        // Handle single click
        event_to_send = EV_BTN_1_PULSE;
    } else if(count == 2){
        // Handle double click
        event_to_send = EV_BTN_2_PULSE;
    } else if(count > 2){
        // Launch emergency
        event_to_send = EV_BTN_EMERGENCY;
    }

    k_msgq_put(&event_queue, &event_to_send, K_NO_WAIT);

}