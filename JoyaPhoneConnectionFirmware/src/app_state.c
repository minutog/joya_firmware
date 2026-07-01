#include "app_state.h"
#include <zephyr/sys/printk.h>

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

static const char *state_name(app_state_t state)
{
    switch (state) {
    case STATE_UNPAIRED: return "UNPAIRED";
    case STATE_SETUP_MODE: return "SETUP_MODE";
    case STATE_SETUP_WAITING_IDENTIFIER: return "SETUP_WAITING_IDENTIFIER";
    case STATE_SETUP_WAITING_NEW_IDENTIFIER: return "SETUP_WAITING_NEW_IDENTIFIER";
    case STATE_WAITING_NOTIFICATION_ENABLE: return "WAITING_NOTIFICATION_ENABLE";
    case STATE_BONDED_DISCONNECTED: return "BONDED_DISCONNECTED";
    case STATE_AUTHENTICATED: return "AUTHENTICATED";
    case STATE_EMERGENCY: return "EMERGENCY";
    default: return "UNKNOWN";
    }
}

static const char *event_name(event_type_t event)
{
    switch (event) {
    case EV_BTN_1_PULSE: return "BTN_1_PULSE";
    case EV_BTN_2_PULSE: return "BTN_2_PULSE";
    case EV_BTN_LONG_PRESS: return "BTN_LONG_PRESS";
    case EV_BTN_EMERGENCY: return "BTN_EMERGENCY";
    case EV_BTN_FACTORY_RESET: return "BTN_FACTORY_RESET";
    case EV_APP_IDENTIFIER_RECEIVED: return "APP_IDENTIFIER_RECEIVED";
    case EV_BLE_CONNECTED: return "BLE_CONNECTED";
    case EV_BLE_DISCONNECTED: return "BLE_DISCONNECTED";
    case EV_BLE_TIMEOUT: return "BLE_TIMEOUT";
    case EV_BLE_NOTIFY_ENABLED: return "BLE_NOTIFY_ENABLED";
    case EV_APP_AUTHENTICATED: return "APP_AUTHENTICATED";
    case EV_APP_STOP_EMERGENCY: return "APP_STOP_EMERGENCY";
    case EV_APP_FOLLOW_ME: return "APP_FOLLOW_ME";
    case EV_APP_ACK_EMERGENCY: return "APP_ACK_EMERGENCY";
    case EV_APP_FRIEND_EMERGENCY: return "APP_FRIEND_EMERGENCY";
    default: return "UNKNOWN";
    }
}

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

    printk("Emergency retry scheduled in %u ms\n", EMERGENCY_RETRY_MS[idx]);
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
        printk("Emergency alerts active, waiting for authentication\n");
        return;
    }

    printk("Emergency alert notify now\n");
    ble_send_event_secure(COMMAND_EMERGENCY);
    emergency_schedule_next_retry();
}

static void emergency_retry_work_handler(struct k_work *work)
{
    if (!is_in_emergency()) {
        return;
    }

    if (!emergency_alerts_active) {
        return;
    }

    if (current_state != STATE_AUTHENTICATED) {
        return;
    }

    ble_send_event_secure(COMMAND_EMERGENCY);
    emergency_schedule_next_retry();
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
    printk("Queue event: %s ret=%d\n", event_name(event), err);
	return err;
}

void fsm_thread_loop(void) {
    k_work_init_delayable(&emergency_retry_work, emergency_retry_work_handler);
    k_work_init_delayable(&connection_timeout_work, connection_timeout_work_handler);
    printk("FSM thread started\n");

    event_type_t event;
    while (1) {
        k_msgq_get(&event_queue, &event, K_FOREVER);
        process_event(event);
    }
}

void app_state_start(void)
{
    int err;

    if (is_app_id_empty()) {
        current_state = STATE_UNPAIRED;
        printk("Initial state: %s. Not advertising yet; double click button to advertise as Joya Setup.\n",
               state_name(current_state));
        return;
    }

    current_state = STATE_BONDED_DISCONNECTED;
    err = ble_start_reconnect_advertising();
    printk("Initial state: %s. Advertising as Joya ret=%d emergency=%d\n",
           state_name(current_state), err, is_in_emergency());
}

/*
 * Button gestures are detected only after firmware initialization.
 * A button press already active during boot is not considered a valid gesture.
 */
void process_event(event_type_t event) {
    app_state_t previous_state = current_state;
    printk("FSM event: %s in state %s emergency=%d app_id_empty=%d\n",
           event_name(event), state_name(current_state),
           is_in_emergency(), is_app_id_empty());

    if (event == EV_BTN_EMERGENCY) {
        storage_save_emergency_state(true);
        haptics_play(HAPTICS_PATTERN_EMERGENCY_START);
        // Note: haptics effect will be played even if the secure channel is not ready, as a warning to the user.

        if (current_state == STATE_AUTHENTICATED) {
            emergency_restart_alerts();
            return;
        }

        k_work_cancel_delayable(&emergency_retry_work);
        reset_emergency_retry_index();

        if (current_state == STATE_BONDED_DISCONNECTED) {
            ble_start_reconnect_advertising();
        } else if (current_state == STATE_UNPAIRED && !is_app_id_empty()) {
            current_state = STATE_BONDED_DISCONNECTED;
            ble_start_reconnect_advertising();
        } else if (current_state == STATE_UNPAIRED && is_app_id_empty()) {
            current_state = STATE_SETUP_MODE;
            ble_start_setup_advertising();
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
            case EV_APP_FOLLOW_ME:
                if (current_state != STATE_AUTHENTICATED) {
                    return;
                }
                haptics_play(HAPTICS_PATTERN_FOLLOW_ME);
                return;
            
            case EV_APP_ACK_EMERGENCY:
                if (current_state != STATE_AUTHENTICATED) {
                    return;
                }
                emergency_stop_alerts();
                return;

            case EV_APP_STOP_EMERGENCY:
                if (current_state != STATE_AUTHENTICATED) {
                    return;
                }
                emergency_stop_alerts();
                storage_save_emergency_state(false);
                return;

            case EV_BLE_TIMEOUT:
                if (is_app_id_empty()) {
                    current_state = STATE_SETUP_MODE;
                    ble_start_setup_advertising();
                } else {
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_reconnect_advertising();
                }

                return;

            case EV_BLE_DISCONNECTED:
                emergency_stop_alerts();

                if (is_app_id_empty()) {
                    current_state = STATE_SETUP_MODE;
                    ble_start_setup_advertising();
                } else {
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_reconnect_advertising();
                }

                return;
            case EV_APP_AUTHENTICATED:
                /*
                * Authenticated again while emergency is still active:
                * restart the alert timing table and send immediately.
                */
                current_state = STATE_AUTHENTICATED;
                emergency_restart_alerts();
                haptics_play(HAPTICS_PATTERN_EMERGENCY_START);

                return;

            case EV_BTN_1_PULSE:
            case EV_BTN_2_PULSE:
            case EV_BTN_LONG_PRESS:
            case EV_APP_FRIEND_EMERGENCY:
            case EV_BTN_FACTORY_RESET:
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
                current_state = STATE_SETUP_MODE;
                haptics_play(HAPTICS_PATTERN_SETUP_MODE);
                ble_start_setup_advertising();
                k_work_reschedule(&connection_timeout_work, K_MSEC(BLE_SETUP_TIMEOUT_MS)); 
            }
            break;

        case STATE_SETUP_MODE:
            if (event == EV_BLE_CONNECTED) {
                k_work_cancel_delayable(&connection_timeout_work);
                current_state = STATE_WAITING_NOTIFICATION_ENABLE;
                ble_stop_advertising();

            } else if (event == EV_BLE_TIMEOUT) {
                current_state = STATE_UNPAIRED;
                ble_stop_advertising();

            } else if (event == EV_BLE_DISCONNECTED) {
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
                current_state = STATE_SETUP_WAITING_IDENTIFIER;
            } else if (event == EV_BLE_DISCONNECTED) {
                if(is_app_id_empty()){
                    current_state = STATE_UNPAIRED;
                } else {
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_reconnect_advertising();
                }
            }
            break;

        case STATE_SETUP_WAITING_IDENTIFIER:
            if(event == EV_APP_IDENTIFIER_RECEIVED){
                if(is_app_id_empty()){
                    // New APP_ID
                    if(save_received_app_id() == 0){
                        current_state = STATE_AUTHENTICATED;
                        // It is safe to send because it is already authenticated
                        ble_send_event_secure(COMMAND_ACK_AUTH);
                        haptics_play_effect(HAPTICS_EFFECT_AUTH);
                        add_event(EV_APP_AUTHENTICATED);
                        return;
                    } else {
                        // In NACK case, it is not necessary to be authenticated
                        ble_send_event_secure(COMMAND_NACK_AUTH);
                    }
                } else {
                    // Already have an APP_ID
                    if(check_app_id(storage_get_app_id()) == 0){
                        current_state = STATE_AUTHENTICATED;
                        ble_send_event_secure(COMMAND_ACK_AUTH);
                        add_event(EV_APP_AUTHENTICATED);
                        haptics_play_effect(HAPTICS_EFFECT_AUTH);
                        return;
                    } else {
                        // (improvement) max retry attempts? or just wait for the next connection?
                        ble_send_event_secure(COMMAND_NACK_AUTH);
                        return;
                    }
                }
            } else if(event == EV_BLE_DISCONNECTED){
                if(is_app_id_empty()){
                    current_state = STATE_UNPAIRED;
                } else {
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_reconnect_advertising();
                }
            }
            break;

        case STATE_AUTHENTICATED:
            // On this state, the device is connected and authenticated with the app. It can send and receive events.
            if (event == EV_APP_FRIEND_EMERGENCY){
                haptics_play(HAPTICS_PATTERN_FRIEND_EMERGENCY);
            } else if (event == EV_BLE_DISCONNECTED) {
                current_state = STATE_BONDED_DISCONNECTED;
                ble_start_reconnect_advertising();

            } else if (event == EV_BTN_1_PULSE) {
                haptics_play(HAPTICS_PATTERN_ROUTINE_START);
                ble_send_event_secure(COMMAND_ROUTINE);

            } else if (event == EV_BTN_LONG_PRESS) {
                haptics_play(HAPTICS_PATTERN_ROUTINE_CANCEL);
                ble_send_event_secure(COMMAND_END_ROUTINE);

            }
            break;

        case STATE_BONDED_DISCONNECTED:
            if (event == EV_BLE_CONNECTED) {
                current_state = STATE_WAITING_NOTIFICATION_ENABLE;
            } 
            break;

        default:
            // (for future use) Handle unexpected state
            break;
    }

    /*
     * Note: factory reset is intentionally ignored while emergency is active.
     * Requirement: emergency handling has priority and must not be interrupted
     * by local reset actions.
     */
    if (event == EV_BTN_FACTORY_RESET) {
        current_state = STATE_UNPAIRED;
        ble_disconnect();
        storage_factory_reset();
        haptics_play_effect(HAPTICS_EFFECT_RESET);
        printk("FSM transition: %s -> %s on %s\n",
               state_name(previous_state), state_name(current_state), event_name(event));
        return;
    }

    if (previous_state != current_state) {
        printk("FSM transition: %s -> %s on %s\n",
               state_name(previous_state), state_name(current_state), event_name(event));
    }
}
