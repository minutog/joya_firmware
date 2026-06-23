#include "app_state.h"

LOG_MODULE_REGISTER(app_state_mod, LOG_LEVEL_INF);

/** @brief Array of retry intervals for emergency events in milliseconds */
const uint32_t EMERGENCY_RETRY_MS[] = {EMERGENCY_RETRY_INITIAL_MS, EMERGENCY_RETRY_FAST_MS, EMERGENCY_RETRY_MEDIUM_MS, EMERGENCY_RETRY_SLOW_MS, EMERGENCY_RETRY_VERY_SLOW_MS};

/** @brief Current index for emergency retry intervals */
uint8_t current_retry_index = 0;

/** @brief Work structure for connection timeout */
static struct k_work_delayable connection_timeout_work;
/** @brief Work structure for emergency retry */
static struct k_work_delayable emergency_retry_work;

static struct k_work_delayable authentification_timeout_work;

/** @brief Current application state */
static volatile app_state_t current_state = STATE_UNPAIRED;
/** @brief Message queue for events */
K_MSGQ_DEFINE(event_queue, sizeof(event_type_t), 10, 4);

/** @brief Thread for the finite state machine */
K_THREAD_DEFINE(fsm_thread_id, 4098, fsm_thread_loop, NULL, NULL, NULL, 5, 0, 0);

extern uint8_t joya_app_id[SIZE_APP_ID];

/**
 * PRIVATE FUNCTIONS
 */

/** @brief Increment the emergency retry index */
void increment_emergency_retry_index(void) {
    if (current_retry_index < sizeof(EMERGENCY_RETRY_MS)/sizeof(EMERGENCY_RETRY_MS[0]) - 1) {
        current_retry_index++;
    }
}

/** @brief Reset the emergency retry index */
void reset_emergency_retry_index(void) {
    current_retry_index = 0;
}

/**
 * WORK HANDLERS
 */

/** @brief Work handler for emergency retry 
 * This function is called when the emergency retry work is executed. It sends the emergency command to the phone and reschedules itself based on the current retry interval.
 * 
*/
static void emergency_retry_work_handler(struct k_work *work) {
    ble_send_event_secure(COMMAND_EMERGENCY);
    k_work_reschedule(&emergency_retry_work, K_MSEC(EMERGENCY_RETRY_MS[current_retry_index]));
    increment_emergency_retry_index();
}

/** @brief Work handler for connection timeout 
 * This function is called when the connection timeout work is executed. It adds a timeout event to the event queue.
*/
static void connection_timeout_work_handler(struct k_work *work) {
    event_type_t ev = EV_BLE_TIMEOUT;
    k_msgq_put(&event_queue, &ev, K_NO_WAIT);
}

/** @brief Work handler for authentification timeout 
 * This function is called when the authentification timeout work is executed. It adds a timeout event to the event queue.
*/
static void authentification_timeout_work_handler(struct k_work *work) {
    event_type_t ev = EV_BLE_TIMEOUT;
    k_msgq_put(&event_queue, &ev, K_NO_WAIT);
}

/**
 * PUBLIC API
 */

app_state_t get_current_state(void) {
    return current_state;
}

int add_event(event_type_t event) {
    return k_msgq_put(&event_queue, &event, K_NO_WAIT);
}

void fsm_thread_loop(void) {
    k_work_init_delayable(&emergency_retry_work, emergency_retry_work_handler);
    k_work_init_delayable(&connection_timeout_work, connection_timeout_work_handler);
    k_work_init_delayable(&authentification_timeout_work, authentification_timeout_work_handler);

    event_type_t event;
    while (1) {
        k_msgq_get(&event_queue, &event, K_FOREVER);
        process_event(event);
    }
}

void scheduler_retry(void){
    k_work_reschedule(&emergency_retry_work, K_MSEC(EMERGENCY_RETRY_MS[current_retry_index]));
}

void process_event(event_type_t event) {
    // Global filter for emergency events
    if(is_in_emergency() && event == EV_BLE_AUTH_AND_NOTIFY){
        ble_send_event_secure(COMMAND_EMERGENCY);
        reset_emergency_retry_index();
        k_work_reschedule(&emergency_retry_work, K_MSEC(EMERGENCY_RETRY_MS[current_retry_index]));
        LOG_INF("FROM: %d TO: STATE_EMERGENCY\n", current_state);
        current_state = STATE_EMERGENCY;
        return;

    } else if(event == EV_BTN_EMERGENCY){ 
        storage_save_emergency_state(true);
        if(current_state == STATE_CONNECTED){
            // (to do) send emergency command to phone and haptic feedback to user (reset timer)
            ble_send_event_secure(COMMAND_EMERGENCY);
            reset_emergency_retry_index();
            k_work_reschedule(&emergency_retry_work, K_MSEC(EMERGENCY_RETRY_MS[current_retry_index]));
            LOG_INF("FROM: %d TO: STATE_EMERGENCY\n", current_state);

        } else if(current_state == STATE_EMERGENCY){
            // (to do) haptic feedback to user?
            reset_emergency_retry_index();
            
        } else if (current_state == STATE_UNPAIRED || current_state == STATE_SETUP_MODE || current_state == STATE_SETUP_WAITING_IDENTIFIER || current_state == STATE_SETUP_WAITING_NEW_IDENTIFIER){
            LOG_INF("REMAINS IN THE SAME STATE BUT SAVES EMERGENCY STATE TO SEND AS SOON AS CONNECTED\n");
            return;
        }
        else {
            // (to do) maybe reforce advertisement procedure to reconnect to the phone faster
            LOG_INF("TO: STATE_EMERGENCY - WAITING TO CONNECT FOR SENDING ALERT\n");
        }
        
        current_state = STATE_EMERGENCY;
        

        // If the device is not connected to the phone, the next EV_BLE_CONNECTED event will trigger the conditional on STATE_EMERGENCY and send the emergency command to the phone. This way, we can ensure that the emergency command is sent as soon as the connection is established

        // Should send a haptic if the phone is not connected?

        return;

    }

    switch (current_state) {
        case STATE_EMERGENCY:
            if(event == EV_BLE_AUTH_AND_NOTIFY){
                // (to do) send emergency command to phone and haptic feedback to user
                // it does not change the state
                ble_send_event_secure(COMMAND_EMERGENCY);
                LOG_INF("FROM: STATE_EMERGENCY TO: STATE_EMERGENCY\n");
                reset_emergency_retry_index();
                k_work_reschedule(&emergency_retry_work, K_MSEC(EMERGENCY_RETRY_MS[current_retry_index]));
            } else if(event == EV_APP_CMD_FOLLOW_ME){
                // (to do) haptic
            }
            else if (event == EV_APP_CMD_STOP_EMERGENCY) {
                // (to do) acknowledge stop emergency command to phone and haptic feedback to user?
                // just in case its stops before ack
                k_work_cancel_delayable(&emergency_retry_work);
                storage_save_emergency_state(false);
                LOG_INF("FROM: STATE_EMERGENCY TO: STATE_CONNECTED\n");
                current_state = STATE_CONNECTED;
            } else if(event == EV_APP_ACK_EMERGENCY){
                // (to do) haptic feedback to user?
                k_work_cancel_delayable(&emergency_retry_work);
                LOG_INF("FROM: STATE_EMERGENCY TO: STATE_EMERGENCY (acknowledged)\n");
            } else if(event == EV_BLE_DISCONNECTED){
                // (to do) maybe reforce advertisement procedure to reconnect to the phone faster
                k_work_cancel_delayable(&emergency_retry_work);
                set_authenticated(false);
                LOG_INF("FROM STATE_EMERGENCY - PHONE DISCONNECTED - STARTING ADVERTISING\n");
                ble_start_reconnect_advertising();
            }
            break;

        case STATE_UNPAIRED:
            if (event == EV_BTN_2_PULSE) {
                set_authenticated(false);
                current_state = STATE_SETUP_MODE;
                LOG_INF("FROM: STATE_UNPAIRED TO: STATE_SETUP_MODE\n");
                ble_start_setup_advertising();
                k_work_reschedule(&connection_timeout_work, K_MSEC(BLE_SETUP_TIMEOUT_MS)); 
            }
            break;

        case STATE_SETUP_MODE:
            if (event == EV_BLE_CONNECTED) {
                current_state = STATE_WAITING_NOTIFICATION_ENABLE;
                LOG_INF("FROM: STATE_SETUP_MODE TO: STATE_WAITING_NOTIFICATION_ENABLE\n");
                ble_stop_advertising();
            } else if (event == EV_BLE_TIMEOUT) {
                current_state = STATE_UNPAIRED;
                LOG_INF("FROM: STATE_SETUP_MODE TO: STATE_UNPAIRED (timeout)\n");
                ble_stop_advertising();
            }
            k_work_cancel_delayable(&connection_timeout_work);
            break;

        case STATE_WAITING_NOTIFICATION_ENABLE:
            if (event == EV_BLE_NOTIFY_ENABLED) {
                if(is_app_id_empty()){
                    current_state = STATE_SETUP_WAITING_NEW_IDENTIFIER;
                    LOG_INF("FROM: WAITING NOTIFICATION ENABLE TO: STATE_SETUP_WAITING_NEW_IDENTIFIER\n");
                } else {
                    current_state = STATE_SETUP_WAITING_IDENTIFIER;
                    LOG_INF("FROM: WAITING NOTIFICATION ENABLE TO: STATE_SETUP_WAITING_IDENTIFIER\n");
                }
            } else if (event == EV_BLE_DISCONNECTED) {
                if(is_app_id_empty()){
                    current_state = STATE_UNPAIRED;
                    LOG_INF("FROM: WAITING NOTIFICATION ENABLE TO: STATE_UNPAIRED (disconnected)\n");
                } else {
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_reconnect_advertising();
                    LOG_INF("FROM: WAITING NOTIFICATION ENABLE TO: STATE_BONDED_DISCONNECTED (reconnecting..)\n");
                }
            }
            break;

        case STATE_SETUP_WAITING_NEW_IDENTIFIER:
            if(event == EV_APP_IDENTIFIER_RECEIVED){
                // Es el nuevo identificador
                // k_work_cancel_delayable(&authentification_timeout_work);
                if(save_received_app_id() == 0){
                    current_state = STATE_CONNECTED;
                    k_work_cancel_delayable(&connection_timeout_work);
                    ble_send_event_secure(COMMAND_ACK_AUTH);
                    LOG_INF("APP_ID SAVED. FROM: STATE_SETUP_WAITING_IDENTIFIER TO STATE_CONNECTED\n");
                    add_event(EV_BLE_AUTH_AND_NOTIFY);
                    return;
                } else {
                    ble_send_event_secure(COMMAND_NACK_AUTH);
                    LOG_INF("APP_ID NOT SAVED. WAITING FOR NEW IDENTIFIER\n");
                }
            } else if(event == EV_BLE_DISCONNECTED){
                current_state = STATE_UNPAIRED;
                ble_force_reset();
                storage_factory_reset();
                current_state = STATE_UNPAIRED;
                LOG_INF("TIMEOUT/DISCONNECTION/RESET WITHOUT CLAIMED. FROM: STATE_SETUP_WAITING_IDENTIFIER TO STATE_UNPAIRED\n");
            }
            break;

        case STATE_SETUP_WAITING_IDENTIFIER:
            if(event == EV_APP_IDENTIFIER_RECEIVED){
                k_work_cancel_delayable(&authentification_timeout_work);
                // Es el nuevo identificador
                if(check_app_id(joya_app_id) == 0){
                    current_state = STATE_CONNECTED;
                    k_work_cancel_delayable(&connection_timeout_work);
                    LOG_INF("APP_ID CHECKED. FROM: STATE_SETUP_WAITING_IDENTIFIER TO STATE_CONNECTED\n");
                    ble_send_event_secure(COMMAND_ACK_AUTH);
                    add_event(EV_BLE_AUTH_AND_NOTIFY);
                    return;
                } else {
                    // current_state = STATE_BONDED_DISCONNECTED;
                    // (to do) max retry attempts? or just wait for the next connection?
                    LOG_INF("WRONG APP_ID. TRY AGAIN\n");
                    ble_send_event_secure(COMMAND_NACK_AUTH);
                    return;
                }
            } else if (event == EV_BLE_DISCONNECTED){
                current_state = STATE_BONDED_DISCONNECTED;
                ble_start_reconnect_advertising();
                LOG_INF("FROM: STATE_SETUP_WAITING_IDENTIFIER TO: STATE_BONDED_DISCONNECTED (reconnecting...)\n");
            }
            break;

        case STATE_CONNECTED:
            if (event == EV_BLE_DISCONNECTED) {
                current_state = STATE_BONDED_DISCONNECTED;
                ble_start_reconnect_advertising();
                LOG_INF("FROM: STATE_CONNECTED TO: STATE_BONDED_DISCONNECTED\n");
            } else if (event == EV_BTN_1_PULSE) {
                // (to do): send routine command to phone
                ble_send_event_secure(COMMAND_ROUTINE);
                LOG_INF("FROM: STATE_CONNECTED TO: STATE_CONNECTED (routine command sent)\n");
            } else if (event == EV_BTN_LONG_PRESS) {
                // (to do): send routine end command to phone
                ble_send_event_secure(COMMAND_END_ROUTINE);
                LOG_INF("FROM: STATE_CONNECTED TO: STATE_CONNECTED (routine end command sent)\n");
            }
            break;

        case STATE_BONDED_DISCONNECTED:
            // (to do): lunch advertisement procedure to reconnect to the phone
            if (event == EV_BLE_CONNECTED) {
                LOG_INF("FROM: STATE_BONDED_DISCONNECTED TO: STATE_WAITING_NOTIFICATION_ENABLE\n");
                current_state = STATE_WAITING_NOTIFICATION_ENABLE;
            } 
            break;

        default:
            // Handle unexpected state
            break;
    }

    if(event == EV_BTN_FACTORY_RESET /*&& current_state != STATE_EMERGENCY*/){
        if(!is_in_emergency()){
            LOG_INF("FROM: %d TO: STATE_UNPAIRED\n", current_state);
            current_state = STATE_UNPAIRED;
            set_authenticated(false);
            ble_force_reset();
            storage_factory_reset();
        } else {
            LOG_INF("FACTORY RESET IGNORED: DEVICE IS IN EMERGENCY STATE\n");
        }
    }
}