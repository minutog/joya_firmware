#include "app_state.h"

/** @brief Array of retry intervals for emergency events in milliseconds */
const uint32_t EMERGENCY_RETRY_MS[] = {EMERGENCY_RETRY_INITIAL_MS, EMERGENCY_RETRY_FAST_MS, EMERGENCY_RETRY_MEDIUM_MS, EMERGENCY_RETRY_SLOW_MS, EMERGENCY_RETRY_VERY_SLOW_MS};
/** @brief Current index for emergency retry intervals */
uint8_t current_retry_index = 0;

static struct k_work_delayable connection_timeout_work;
static struct k_work_delayable emergency_retry_work;

/** @brief Current application state */
static volatile app_state_t current_state = STATE_UNPAIRED;
/** @brief Message queue for events */
K_MSGQ_DEFINE(event_queue, sizeof(event_type_t), 20, 4);

/** @brief Thread for the finite state machine */
K_THREAD_DEFINE(fsm_thread_id, 4098, fsm_thread_loop, NULL, NULL, NULL, 5, 0, 0);

static bool emergency_alerts_active = false;

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
/*
 * Improvement: route emergency retry work through the FSM to avoid shared state between workqueue and process_event().
 */
void reset_emergency_retry_index(void) {
    current_retry_index = 0;
}

static void emergency_stop_alerts(void)
{
    emergency_alerts_active = false;
    k_work_cancel_delayable(&emergency_retry_work);
    reset_emergency_retry_index();
}


/**
 * WORK HANDLERS
 */

static void emergency_schedule_next_retry(void)
{
    uint8_t idx = current_retry_index;

    k_work_reschedule(&emergency_retry_work,
                      K_MSEC(EMERGENCY_RETRY_MS[idx]));

    increment_emergency_retry_index();
}

static void emergency_restart_alerts(void)
{
    emergency_alerts_active = true;

    k_work_cancel_delayable(&emergency_retry_work);
    reset_emergency_retry_index();

    if (current_state != STATE_AUTHENTICATED) {
        return;
    }

    // LOG: Sending emergency event
    ble_send_event_secure(COMMAND_EMERGENCY);
    emergency_schedule_next_retry();
}

static void emergency_retry_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    add_event(EV_EMERGENCY_RETRY);
}


/** @brief Work handler for connection timeout 
 * This function is called when the connection timeout work is executed. It adds a timeout event to the event queue.
*/
static void connection_timeout_work_handler(struct k_work *work) {
    event_type_t ev = EV_BLE_TIMEOUT;
    k_msgq_put(&event_queue, &ev, K_NO_WAIT);
}


/**
 * PUBLIC API
 */

app_state_t get_current_state(void) {
    return current_state;
}


int add_event(event_type_t event)
{
	int err = k_msgq_put(&event_queue, &event, K_NO_WAIT);
	return err;
}

void fsm_thread_loop(void) {
    k_work_init_delayable(&emergency_retry_work, emergency_retry_work_handler);
    k_work_init_delayable(&connection_timeout_work, connection_timeout_work_handler);

    event_type_t event;
    while (1) {
        k_msgq_get(&event_queue, &event, K_FOREVER);
        process_event(event);
    }
}

/*
 * Button gestures are detected only after firmware initialization.
 * A button press already active during boot is not considered a valid gesture.
 */
void process_event(event_type_t event) {
    int ret;

    /*
     * Note: factory reset has a higher priority that everything else
     * to fulfill the requirement updated in previous conversation.
     * This is a point to consider in future updates
     */
    if (event == EV_BTN_FACTORY_RESET) {
        // LOG: Factory reset button pressed - resetting device
        ret = ble_send_event_secure(COMMAND_FACTORY_RESET);
        if(ret != 0){
            // LOG: Failed to send FACTORY_RESET - continuing without sending
        }
        current_state = STATE_UNPAIRED;
        ble_disconnect();
        ret = storage_factory_reset();
        emergency_stop_alerts();
        if(ret == 1) {
            // LOG: Flash write failed while resetting flash storage - reseting only RAM state
        }

        haptics_play_effect(HAPTICS_EFFECT_RESET);
        return;
    }

    if (event == EV_BTN_EMERGENCY) {
        // LOG: Emergency button pressed
        ret = storage_save_emergency_state(true);
        if (ret == 1) {
            // LOG: Failed to save emergency state in flash - continuing without saving
        }

        haptics_play(HAPTICS_PATTERN_EMERGENCY_START);
        // Note: haptics effect will be played even if the secure channel is not ready, as a warning to the user.

        if (current_state == STATE_AUTHENTICATED) {
            // LOG: Sending emergency
            emergency_restart_alerts();
            return;
        }

        // LOG: Emergency button pressed but not authenticated - scheduling retry
        k_work_cancel_delayable(&emergency_retry_work);
        reset_emergency_retry_index();

        if (current_state == STATE_BONDED_DISCONNECTED) {
            ble_start_setup_advertising(false);
        } else if (current_state == STATE_UNPAIRED && !is_app_id_empty()) {
            current_state = STATE_BONDED_DISCONNECTED;
            ble_start_setup_advertising(false);
        } else if (current_state == STATE_UNPAIRED && is_app_id_empty()) {
            current_state = STATE_SETUP_MODE;
            ble_start_setup_advertising(true);
        }

        return;
    }

    /*
    * Note: Emergency can be triggered locally before the app is authenticated.
    * However, app-originated emergency commands are only accepted after the
    * secure channel is ready.
    */
    if (is_in_emergency()) {
        switch (event) {
            case EV_EMERGENCY_RETRY:
                if (!is_in_emergency()) {
                    return;
                }

                if (!emergency_alerts_active) {
                    return;
                }

                if (current_state != STATE_AUTHENTICATED) {
                    return;
                }

                // LOG: Retrying emergency event

                ret = ble_send_event_secure(COMMAND_EMERGENCY);
                if (ret != 0) {
                    // LOG: Failed to resend COMMAND_EMERGENCY - continuing
                }

                emergency_schedule_next_retry();
                return;

            case EV_APP_FOLLOW_ME:
                if (current_state != STATE_AUTHENTICATED) {
                    return;
                }
                ret = ble_send_event_secure(COMMAND_ACK);
                if(ret != 0){
                    // LOG: Failed to send ACK - continuing without sending
                }
                // LOG: Follow me event received
                haptics_play(HAPTICS_PATTERN_FOLLOW_ME);
                return;
            
            case EV_APP_ACK_EMERGENCY:
                if (current_state != STATE_AUTHENTICATED) {
                    // LOG: Acknowledgment received but not authenticated
                    return;
                }
                // LOG: Acknowledged emergency event received
                emergency_stop_alerts();
                ret = ble_send_event_secure(COMMAND_ACK);
                if(ret != 0){
                    // LOG: Failed to send ACK - continuing without sending
                }
                return;

            case EV_APP_STOP_EMERGENCY:
                if (current_state != STATE_AUTHENTICATED) {
                    // LOG: Stop emergency received but not authenticated
                    return;
                }
                // LOG: Stop emergency event received
                emergency_stop_alerts();
                ret = storage_save_emergency_state(false);
                if (ret == 1) {
                    // LOG: Failed to save emergency state in flash - continuing without saving
                }
                ret = ble_send_event_secure(COMMAND_ACK);
                if(ret != 0){
                    // LOG: Failed to send ACK - continuing without sending
                }
                return;

            case EV_BLE_TIMEOUT:
                // LOG: BLE timeout
                if (is_app_id_empty()) {
                    // LOG: No APP_ID stored - entering setup mode (starting ADV)
                    current_state = STATE_SETUP_MODE;
                    ble_start_setup_advertising(true);
                } else {
                    // LOG: APP_ID stored - entering bonded disconnected state (starting ADV)
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_setup_advertising(false);
                }

                return;

            case EV_BLE_DISCONNECTED:
                // LOG: BLE disconnected
                emergency_stop_alerts();

                if (is_app_id_empty()) {
                    // LOG: No APP_ID stored - entering setup mode (starting ADV)
                    current_state = STATE_SETUP_MODE;
                    ble_start_setup_advertising(true);
                } else {
                    // LOG: APP_ID stored - entering bonded disconnected state (starting ADV)
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_setup_advertising(false);
                }

                return;
            case EV_APP_AUTHENTICATED:
                /*
                * Authenticated again while emergency is still active:
                * restart the alert timing table and send immediately.
                */
                // LOG: App authenticated - restarting alerts
                current_state = STATE_AUTHENTICATED;
                emergency_restart_alerts();
                haptics_play(HAPTICS_PATTERN_EMERGENCY_START);

                return;

            case EV_BTN_1_PULSE:
            /* EV_BTN_2_PULSE is intentionally not ignored here,
            * allowing reconnection while an emergency is active.
            */
            case EV_BTN_LONG_PRESS:
            case EV_APP_FRIEND_EMERGENCY:
                // LOG: Ignoring event while emergency is active
                /*
                * Normal app actions are ignored while emergency is active.
                */
                return;

            default:
                // Keep processing other events that are not related to emergency
                break;
        }
    }

    switch (current_state) {
        case STATE_UNPAIRED:
            if (event == EV_BTN_2_PULSE) {
                // LOG: Entering setup mode (starting ADV)
                current_state = STATE_SETUP_MODE;
                haptics_play(HAPTICS_PATTERN_SETUP_MODE);
                ble_start_setup_advertising(is_app_id_empty());
                k_work_reschedule(&connection_timeout_work, K_MSEC(BLE_SETUP_TIMEOUT_MS)); 
            }
            break;

        case STATE_SETUP_MODE:
            if (event == EV_BLE_CONNECTED) {
                // LOG: BLE connected - waiting for notification enable
                k_work_cancel_delayable(&connection_timeout_work);
                current_state = STATE_WAITING_NOTIFICATION_ENABLE;
                ble_stop_advertising();

            } else if (event == EV_BLE_TIMEOUT) {
                // LOG: BLE setup timeout in setup mode - returning to unpaired state
                current_state = STATE_UNPAIRED;
                ble_stop_advertising();

            } else if (event == EV_BLE_DISCONNECTED) {
                // LOG: BLE disconnected in setup mode - returning to unpaired state
                k_work_cancel_delayable(&connection_timeout_work);
                current_state = STATE_UNPAIRED;
                ble_stop_advertising();
            }
            break;

        case STATE_WAITING_NOTIFICATION_ENABLE:
            /*
            * Protocol contract: the app must enable notifications before sending
            * APP_ID. Identifier events received in this state are intentionally not
            * handled; the app must resend APP_ID after the firmware reaches
            * STATE_SETUP_WAITING_IDENTIFIER.
            */
            if (event == EV_BLE_NOTIFY_ENABLED) {
                // LOG: Notifications enabled - waiting for APP_ID
                current_state = STATE_SETUP_WAITING_IDENTIFIER;
            } else if (event == EV_BLE_DISCONNECTED) {
                if(is_app_id_empty()){
                    // LOG: BLE disconnected in waiting notification enable state - returning to unpaired state
                    current_state = STATE_UNPAIRED;
                } else {
                    // LOG: BLE disconnected in waiting notification enable state - returning to bonded disconnected state (starting ADV)
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_setup_advertising(false);
                }
            }
            break;

        case STATE_SETUP_WAITING_IDENTIFIER:
            if(event == EV_APP_IDENTIFIER_RECEIVED){
                // LOG: APP_ID received - checking validity
                if(is_app_id_empty()){
                    // LOG: No APP_ID stored - saving new APP_ID
                    // New APP_ID
                    ret = save_received_app_id();
                    if(ret == 1) {
                        // LOG: Flash write failed while saving new APP_ID - continuing with RAM state
                    }
                    
                    // LOG: New APP_ID saved - sending ACK
                    current_state = STATE_AUTHENTICATED;
                    // It is safe to send because it is already authenticated
                    ret = ble_send_event_secure(COMMAND_ACK);
                    if(ret != 0){
                        // LOG: Failed to send ACK - continuing without sending
                    }

                    haptics_play_effect(HAPTICS_EFFECT_AUTH);
                    add_event(EV_APP_AUTHENTICATED);
                    return;
                    
                } else {
                    // LOG: APP_ID already stored - checking if it matches received APP_ID
                    // Already have an APP_ID
                    if(check_app_id(storage_get_app_id()) == 0){
                        // LOG: APP_ID matches stored APP_ID - sending ACK
                        current_state = STATE_AUTHENTICATED;
                        ret = ble_send_event_secure(COMMAND_ACK);
                        if(ret != 0){
                            // LOG: Failed to send ACK - continuing without sending
                        }
                        add_event(EV_APP_AUTHENTICATED);
                        haptics_play_effect(HAPTICS_EFFECT_AUTH);
                        return;
                    } else {
                        // LOG: APP_ID does not match stored APP_ID - sending NACK
                        // (improvement) max retry attempts? or just wait for the next connection?
                        ret = ble_send_event_secure(COMMAND_NACK);
                        if(ret != 0){
                            // LOG: Failed to send NACK - continuing without sending
                        }
                        return;
                    }
                }
            } else if(event == EV_BLE_DISCONNECTED){
                if(is_app_id_empty()){
                    // LOG: BLE disconnected while waiting for the new APP_ID - returning to unpaired state
                    current_state = STATE_UNPAIRED;
                } else {
                    // LOG: BLE disconnected while waiting for the saved APP_ID - returning to bonded disconnected state (starting ADV)
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_setup_advertising(false);
                }
            }
            break;

        case STATE_AUTHENTICATED:
            // On this state, the device is connected and authenticated with the app. It can send and receive events.
            if (event == EV_APP_FRIEND_EMERGENCY){
                // LOG: Friend emergency event received
                haptics_play(HAPTICS_PATTERN_FRIEND_EMERGENCY);
                ret = ble_send_event_secure(COMMAND_ACK);
                if(ret != 0){
                    // LOG: Failed to send ACK - continuing without sending
                }
            } else if (event == EV_BLE_DISCONNECTED) {
                // LOG: BLE disconnected - returning to bonded disconnected state (starting ADV)
                current_state = STATE_BONDED_DISCONNECTED;
                ble_start_setup_advertising(false);

            } else if (event == EV_BTN_1_PULSE) {
                // LOG: Routine start button pressed
                haptics_play(HAPTICS_PATTERN_ROUTINE_START);
                ret = ble_send_event_secure(COMMAND_ROUTINE);
                if (ret != 0) {
                    // LOG: Failed to send COMMAND_ROUTINE - continuing without sending
                }

            } else if (event == EV_BTN_LONG_PRESS) {
                // LOG: Routine cancel button pressed
                haptics_play(HAPTICS_PATTERN_ROUTINE_CANCEL);
                ret = ble_send_event_secure(COMMAND_END_ROUTINE);
                if (ret != 0) {
                    // LOG: Failed to send COMMAND_END_ROUTINE - continuing without sending
                }

            }
            break;

        case STATE_BONDED_DISCONNECTED:
            if (event == EV_BLE_CONNECTED) {
                // LOG: BLE connected - waiting for notification enable
                current_state = STATE_WAITING_NOTIFICATION_ENABLE;
            } 
            break;

        default:
            // (for future use) Handle unexpected state
            break;
    }
}
