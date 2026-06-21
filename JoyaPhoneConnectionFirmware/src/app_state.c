#include "app_state.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_state_mod, LOG_LEVEL_INF);

static volatile app_state_t current_state = STATE_UNPAIRED;
K_MSGQ_DEFINE(event_queue, sizeof(event_type_t), 10, 4);

app_state_t get_current_state(void) {
    return current_state;
}

void fsm_thread_loop(void) {
    event_type_t event;
    while (1) {
        k_msgq_get(&event_queue, &event, K_FOREVER);
        process_event(event);
    }
}

K_THREAD_DEFINE(fsm_thread_id, 1024, fsm_thread_loop, NULL, NULL, NULL, 5, 0, 0);

void process_event(event_type_t event) {
    // Global filter for emergency events
    if(event == EV_BTN_EMERGENCY && current_state != STATE_UNPAIRED) {
        if(current_state == STATE_CONNECTED || current_state == STATE_EMERGENCY){
            // (to do) send emergency command to phone and haptic feedback to user (reset timer)

        } else if(current_state == STATE_BONDED_DISCONNECTED){
            // (to do) maybe reforce advertisement procedure to reconnect to the phone faster
        }

        current_state = STATE_EMERGENCY;

        // If the device is not connected to the phone, the next EV_BLE_CONNECTED event will trigger the conditional on STATE_EMERGENCY and send the emergency command to the phone. This way, we can ensure that the emergency command is sent as soon as the connection is established

        // Should send a haptic if the phone is not connected?

        return;

    }

    switch (current_state) {
        case STATE_UNPAIRED:
            if (event == EV_BTN_2_PULSE) {
                current_state = STATE_SETUP_MODE;
                printk("FROM: STATE_UNPAIRED TO: STATE_SETUP_MODE\n");
            }
            break;

        case STATE_SETUP_MODE:
            if (event == EV_BLE_CONNECTED) {
                current_state = STATE_CONNECTED;
                printk("FROM: STATE_SETUP_MODE TO: STATE_CONNECTED\n");
            } else if (event == EV_BTN_FACTORY_RESET || event == EV_BLE_TIMEOUT) {
                current_state = STATE_UNPAIRED;
                printk("FROM: STATE_SETUP_MODE TO: STATE_UNPAIRED\n");
            }
            break;

        case STATE_CONNECTED:
            if (event == EV_BLE_DISCONNECTED) {
                current_state = STATE_BONDED_DISCONNECTED;
                printk("FROM: STATE_CONNECTED TO: STATE_BONDED_DISCONNECTED\n");
            } else if (event == EV_BTN_1_PULSE) {
                // (to do): send routine command to phone
                printk("FROM: STATE_CONNECTED TO: STATE_CONNECTED (routine command sent)\n");
            } else if (event == EV_BTN_LONG_PRESS) {
                // (to do): send routine end command to phone
                printk("FROM: STATE_CONNECTED TO: STATE_CONNECTED (routine end command sent)\n");
            } else if (event == EV_BTN_FACTORY_RESET) {
                // (to do): lunch factory reset procedure
                printk("FROM: STATE_CONNECTED TO: STATE_UNPAIRED \n");
                current_state = STATE_UNPAIRED;
            }
            break;

        case STATE_BONDED_DISCONNECTED:
            // (to do): lunch advertisement procedure to reconnect to the phone
            if (event == EV_BLE_CONNECTED) {
                printk("FROM: STATE_BONDED_DISCONNECTED TO: STATE_CONNECTED\n");
                current_state = STATE_CONNECTED;
            } else if (event == EV_BTN_FACTORY_RESET) {
                // (to do): lunch factory reset procedure
                printk("FROM: STATE_BONDED_DISCONNECTED TO: STATE_UNPAIRED\n");
                current_state = STATE_UNPAIRED;
            }
            break;

        case STATE_EMERGENCY:
            if(event == EV_BLE_CONNECTED){
                // (to do) send emergency command to phone and haptic feedback to user
                // it does not change the state
                printk("FROM: STATE_EMERGENCY TO: STATE_EMERGENCY\n");
            }
            else if (event == EV_APP_CMD_STOP_EMERGENCY) {
                // (to do) acknowledge stop emergency command to phone and haptic feedback to user?
                printk("FROM: STATE_EMERGENCY TO: STATE_CONNECTED\n");
                current_state = STATE_CONNECTED;
            }
            break;

        default:
            // Handle unexpected state
            break;
    }
}