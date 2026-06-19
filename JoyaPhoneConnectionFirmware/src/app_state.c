#include "app_state.h"

static volatile app_state_t current_state = STATE_UNPAIRED;

app_state_t get_current_state(void) {
    return current_state;
}

void process_event(event_type_t event) {
    // Global filter for emergency events
    if(event == EV_BTN_EMERGENCY) {
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
            }
            break;

        case STATE_SETUP_MODE:
            if (event == EV_BLE_CONNECTED) {
                current_state = STATE_CONNECTED;
            } else if (event == EV_BTN_FACTORY_RESET || event == EV_BLE_TIMEOUT) {
                current_state = STATE_UNPAIRED;
            }
            break;

        case STATE_CONNECTED:
            if (event == EV_BLE_DISCONNECTED) {
                current_state = STATE_BONDED_DISCONNECTED;
            } else if (event == EV_BTN_1_PULSE) {
                // (to do): send routine command to phone
            } else if (event == EV_BTN_LONG_PRESS) {
                // (to do): send routine end command to phone
            } else if (event == EV_BTN_FACTORY_RESET) {
                // (to do): lunch factory reset procedure
                current_state = STATE_UNPAIRED;
            }
            break;

        case STATE_BONDED_DISCONNECTED:
            // (to do): lunch advertisement procedure to reconnect to the phone
            if (event == EV_BLE_CONNECTED) {
                current_state = STATE_CONNECTED;
            } else if (event == EV_BTN_FACTORY_RESET) {
                // (to do): lunch factory reset procedure
                current_state = STATE_UNPAIRED;
            }
            break;

        case STATE_EMERGENCY:
            if(event == EV_BLE_CONNECTED){
                // (to do) send emergency command to phone and haptic feedback to user
                // it does not change the state
            }
            else if (event == EV_APP_CMD_STOP_EMERGENCY) {
                // (to do) acknowledge stop emergency command to phone and haptic feedback to user?
                current_state = STATE_CONNECTED;
            } else if(event == EV_BTN_EMERGENCY){
                // (to do) reset the emergency command timer
            } 
            break;

        default:
            // Handle unexpected state
            break;
    }
}