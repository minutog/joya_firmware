#include "app_state.h"
#include "app_comm.h"
#include "ble_driver.h"

#include <zephyr/logging/log.h>
const uint32_t EMERGENCY_RETRY_MS[] = {1000, 2000, 5000, 10000, 30000}; // Retry intervals in milliseconds
uint8_t current_retry_index = 0;

LOG_MODULE_REGISTER(app_state_mod, LOG_LEVEL_INF);

static struct k_work_delayable emergency_retry_work;
extern uint8_t current_retry_index;

static volatile app_state_t current_state = STATE_UNPAIRED;
K_MSGQ_DEFINE(event_queue, sizeof(event_type_t), 10, 4);

void increment_emergency_retry_index(void) {
    if (current_retry_index < sizeof(EMERGENCY_RETRY_MS)/sizeof(EMERGENCY_RETRY_MS[0]) - 1) {
        current_retry_index++;
    }
}

static void emergency_retry_work_handler(struct k_work *work) {
    // This function is called when the emergency retry timer expires
    // We will send the emergency command again and reschedule the work
    ble_send_event_secure(COMMAND_EMERGENCY);
    k_work_reschedule(&emergency_retry_work, K_MSEC(EMERGENCY_RETRY_MS[current_retry_index]));
    increment_emergency_retry_index();
}

void reset_emergency_retry_index(void) {
    current_retry_index = 0;
}


app_state_t get_current_state(void) {
    return current_state;
}

void fsm_thread_loop(void) {
    k_work_init_delayable(&emergency_retry_work, emergency_retry_work_handler);

    event_type_t event;
    while (1) {
        k_msgq_get(&event_queue, &event, K_FOREVER);
        process_event(event);
    }
}

K_THREAD_DEFINE(fsm_thread_id, 1024, fsm_thread_loop, NULL, NULL, NULL, 5, 0, 0);



void scheduler_retry(void){
    k_work_reschedule(&emergency_retry_work, K_MSEC(EMERGENCY_RETRY_MS[current_retry_index]));
}

void process_event(event_type_t event) {
    // Global filter for emergency events
    if(event == EV_BTN_EMERGENCY && current_state != STATE_UNPAIRED) {
        if(current_state == STATE_CONNECTED || current_state == STATE_EMERGENCY){
            // (to do) send emergency command to phone and haptic feedback to user (reset timer)
            ble_send_event_secure(COMMAND_EMERGENCY);
            reset_emergency_retry_index();
            k_work_reschedule(&emergency_retry_work, K_MSEC(EMERGENCY_RETRY_MS[current_retry_index]));
            printk("FROM: %d TO: STATE_EMERGENCY\n", current_state);

        } else if(current_state == STATE_BONDED_DISCONNECTED){
            // (to do) maybe reforce advertisement procedure to reconnect to the phone faster
            printk("FROM: STATE_BONDED_DISCONNECTED TO: STATE_EMERGENCY\n");
        }

        current_state = STATE_EMERGENCY;

        // If the device is not connected to the phone, the next EV_BLE_CONNECTED event will trigger the conditional on STATE_EMERGENCY and send the emergency command to the phone. This way, we can ensure that the emergency command is sent as soon as the connection is established

        // Should send a haptic if the phone is not connected?

        return;

    }

    switch (current_state) {
        case STATE_EMERGENCY:
            if(event == EV_BLE_CONNECTED){
                // (to do) send emergency command to phone and haptic feedback to user
                // it does not change the state
                ble_send_event_secure(COMMAND_EMERGENCY);
                printk("FROM: STATE_EMERGENCY TO: STATE_EMERGENCY\n");
                reset_emergency_retry_index();
                k_work_reschedule(&emergency_retry_work, K_MSEC(EMERGENCY_RETRY_MS[current_retry_index]));
            } else if(event == EV_APP_CMD_FOLLOW_ME){
                // (to do) haptic
            }
            else if (event == EV_APP_CMD_STOP_EMERGENCY) {
                // (to do) acknowledge stop emergency command to phone and haptic feedback to user?
                printk("FROM: STATE_EMERGENCY TO: STATE_CONNECTED\n");
                current_state = STATE_CONNECTED;
            } else if(event == EV_APP_ACK_EMERGENCY){
                // (to do) haptic feedback to user?
                k_work_cancel_delayable(&emergency_retry_work);
                reset_emergency_retry_index();
                printk("FROM: STATE_EMERGENCY TO: STATE_EMERGENCY (acknowledged)\n");
            }
            break;

        case STATE_UNPAIRED:
            if (event == EV_BTN_2_PULSE) {
                current_state = STATE_SETUP_MODE;
                printk("FROM: STATE_UNPAIRED TO: STATE_SETUP_MODE\n");
                ble_start_setup_advertising();
            }
            break;

        case STATE_SETUP_MODE:
            if (event == EV_BLE_CONNECTED) {
                current_state = STATE_CONNECTED;
                printk("FROM: STATE_SETUP_MODE TO: STATE_CONNECTED\n");
                ble_stop_advertising();
            }
            break;

        case STATE_CONNECTED:
            if (event == EV_BLE_DISCONNECTED) {
                current_state = STATE_BONDED_DISCONNECTED;
                ble_start_reconnect_advertising();
                printk("FROM: STATE_CONNECTED TO: STATE_BONDED_DISCONNECTED\n");
            } else if (event == EV_BTN_1_PULSE) {
                // (to do): send routine command to phone
                ble_send_event_secure(COMMAND_ROUTINE);
                printk("FROM: STATE_CONNECTED TO: STATE_CONNECTED (routine command sent)\n");
            } else if (event == EV_BTN_LONG_PRESS) {
                // (to do): send routine end command to phone
                ble_send_event_secure(COMMAND_END_ROUTINE);
                printk("FROM: STATE_CONNECTED TO: STATE_CONNECTED (routine end command sent)\n");
            }
            break;

        case STATE_BONDED_DISCONNECTED:
            // (to do): lunch advertisement procedure to reconnect to the phone
            if (event == EV_BLE_CONNECTED) {
                printk("FROM: STATE_BONDED_DISCONNECTED TO: STATE_CONNECTED\n");
                current_state = STATE_CONNECTED;
            } 
            break;

        default:
            // Handle unexpected state
            break;
    }

    if(event == EV_BTN_FACTORY_RESET && current_state != STATE_EMERGENCY){
        printk("FROM: %d TO: STATE_UNPAIRED\n", current_state);
        current_state = STATE_UNPAIRED;
        ble_force_reset();
    }
}