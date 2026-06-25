#include "app_state.h"

LOG_MODULE_REGISTER(app_state_mod, LOG_LEVEL_INF);

/** @brief Array of retry intervals for emergency events in milliseconds */
const uint32_t EMERGENCY_RETRY_MS[] = {EMERGENCY_RETRY_INITIAL_MS, EMERGENCY_RETRY_FAST_MS, EMERGENCY_RETRY_MEDIUM_MS, EMERGENCY_RETRY_SLOW_MS, EMERGENCY_RETRY_VERY_SLOW_MS};
/** @brief Current index for emergency retry intervals */
uint8_t current_retry_index = 0;

static struct k_work_delayable connection_timeout_work;
static struct k_work_delayable emergency_retry_work;

/** @brief Current application state */
static volatile app_state_t current_state = STATE_UNPAIRED;
/** @brief Message queue for events */
K_MSGQ_DEFINE(event_queue, sizeof(event_type_t), 10, 4);

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

	if (err == -ENOMSG) {
		LOG_ERR("Event queue full, dropped event %d", event);
	} else if (err) {
		LOG_ERR("Failed to enqueue event %d: %d", event, err);
	}
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

/** FOR DEBUGGING */
static const char *event_to_str(event_type_t event)
{
    switch (event) {
    case EV_BTN_EMERGENCY: return "EV_BTN_EMERGENCY";
    case EV_BTN_1_PULSE: return "EV_BTN_1_PULSE";
    case EV_BTN_2_PULSE: return "EV_BTN_2_PULSE";
    case EV_BTN_LONG_PRESS: return "EV_BTN_LONG_PRESS";
    case EV_BTN_FACTORY_RESET: return "EV_BTN_FACTORY_RESET";
    case EV_BLE_CONNECTED: return "EV_BLE_CONNECTED";
    case EV_BLE_DISCONNECTED: return "EV_BLE_DISCONNECTED";
    case EV_BLE_NOTIFY_ENABLED: return "EV_BLE_NOTIFY_ENABLED";
    case EV_APP_IDENTIFIER_RECEIVED: return "EV_APP_IDENTIFIER_RECEIVED";
    case EV_APP_AUTHENTICATED: return "EV_APP_AUTHENTICATED";
    case EV_APP_ACK_EMERGENCY: return "EV_APP_ACK_EMERGENCY";
    case EV_APP_STOP_EMERGENCY: return "EV_APP_STOP_EMERGENCY";
    case EV_APP_FOLLOW_ME: return "EV_APP_FOLLOW_ME";
    case EV_BLE_TIMEOUT: return "EV_BLE_TIMEOUT";
    case EV_APP_FRIEND_EMERGENCY: return "EV_APP_FRIEND_EMERGENCY";
    default: return "UNKNOWN_EVENT";
    }
}
/********** */

/*
 * Button gestures are detected only after firmware initialization.
 * A button press already active during boot is not considered a valid gesture.
 */
void process_event(event_type_t event) {
    // Global filter for emergency events
    LOG_INF("process_event: %s, state=%d, emergency=%d, alerts_active=%d, retry_idx=%d",
        event_to_str(event),
        current_state,
        is_in_emergency(),
        emergency_alerts_active,
        current_retry_index);

    if (event == EV_BTN_EMERGENCY) {
        storage_save_emergency_state(true);
        haptics_play(HAPTICS_PATTERN_EMERGENCY_START);
        // Note: haptics effect will be played even if the secure channel is not ready, as a warning to the user.

        if (current_state == STATE_AUTHENTICATED) {
            LOG_INF("Emergency triggered/re-triggered while secure channel ready");
            emergency_restart_alerts();
            return;
        }

        k_work_cancel_delayable(&emergency_retry_work);
        reset_emergency_retry_index();

        LOG_INF("Emergency active, waiting for secure channel");

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
                    LOG_WRN("FOLLOW_ME ignored: emergency active but app not authenticated");
                    return;
                }
                haptics_play(HAPTICS_PATTERN_FOLLOW_ME);
                return;
            
            case EV_APP_ACK_EMERGENCY:
                if (current_state != STATE_AUTHENTICATED) {
                    LOG_WRN("FOLLOW_ME ignored: emergency active but app not authenticated");
                    return;
                }
                emergency_stop_alerts();
                LOG_INF("Emergency acknowledged, retries stopped");
                return;

            case EV_APP_STOP_EMERGENCY:
                if (current_state != STATE_AUTHENTICATED) {
                    LOG_WRN("FOLLOW_ME ignored: emergency active but app not authenticated");
                    return;
                }
                emergency_stop_alerts();
                storage_save_emergency_state(false);
                LOG_INF("Emergency stopped");
                return;

            case EV_BLE_TIMEOUT:
                LOG_INF("Emergency active: BLE timeout, keeping/restarting advertising");

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
                    LOG_INF("Emergency active: disconnected, restarting setup advertising");
                } else {
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_reconnect_advertising();
                    LOG_INF("Emergency active: disconnected, reconnecting");
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

                LOG_INF("Emergency active after auth, alerts restarted");
                return;

            case EV_BTN_1_PULSE:
            case EV_BTN_2_PULSE:
            case EV_BTN_LONG_PRESS:
            case EV_APP_FRIEND_EMERGENCY:
            case EV_BTN_FACTORY_RESET:
                /*
                * Normal app actions are ignored while emergency is active.
                */
                LOG_INF("Event ignored: emergency active");
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
                LOG_INF("FROM: STATE_UNPAIRED TO: STATE_SETUP_MODE\n");
                haptics_play(HAPTICS_PATTERN_SETUP_MODE);
                ble_start_setup_advertising();
                k_work_reschedule(&connection_timeout_work, K_MSEC(BLE_SETUP_TIMEOUT_MS)); 
            }
            break;

        case STATE_SETUP_MODE:
            if (event == EV_BLE_CONNECTED) {
                k_work_cancel_delayable(&connection_timeout_work);
                current_state = STATE_WAITING_NOTIFICATION_ENABLE;
                LOG_INF("FROM: STATE_SETUP_MODE TO: STATE_WAITING_NOTIFICATION_ENABLE\n");
                ble_stop_advertising();

            } else if (event == EV_BLE_TIMEOUT) {
                current_state = STATE_UNPAIRED;
                LOG_INF("FROM: STATE_SETUP_MODE TO: STATE_UNPAIRED (timeout)\n");
                ble_stop_advertising();

            } else if (event == EV_BLE_DISCONNECTED) {
                k_work_cancel_delayable(&connection_timeout_work);
                current_state = STATE_UNPAIRED;
                LOG_INF("FROM: STATE_SETUP_MODE TO: STATE_UNPAIRED (disconnected)\n");
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
                LOG_INF("FROM: WAITING NOTIFICATION ENABLE TO: STATE_SETUP_WAITING_IDENTIFIER\n");
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

        case STATE_SETUP_WAITING_IDENTIFIER:
            if(event == EV_APP_IDENTIFIER_RECEIVED){
                if(is_app_id_empty()){
                    // New APP_ID
                    if(save_received_app_id() == 0){
                        current_state = STATE_AUTHENTICATED;
                        // It is safe to send because it is already authenticated
                        ble_send_event_secure(COMMAND_ACK_AUTH);
                        haptics_play_effect(HAPTICS_EFFECT_AUTH);
                        LOG_INF("APP_ID SAVED. FROM: STATE_SETUP_WAITING_IDENTIFIER TO STATE_AUTHENTICATED\n");
                        add_event(EV_APP_AUTHENTICATED);
                        return;
                    } else {
                        // In NACK case, it is not necessary to be authenticated
                        ble_send_event_secure(COMMAND_NACK_AUTH);
                        LOG_INF("APP_ID NOT SAVED. WAITING FOR NEW IDENTIFIER\n");
                    }
                } else {
                    // Already have an APP_ID
                    if(check_app_id(storage_get_app_id()) == 0){
                        current_state = STATE_AUTHENTICATED;
                        ble_send_event_secure(COMMAND_ACK_AUTH);
                        add_event(EV_APP_AUTHENTICATED);
                        haptics_play_effect(HAPTICS_EFFECT_AUTH);
                        LOG_INF("APP_ID CHECKED. FROM: STATE_SETUP_WAITING_IDENTIFIER TO STATE_AUTHENTICATED\n");
                        return;
                    } else {
                        // (improvement) max retry attempts? or just wait for the next connection?
                        LOG_INF("WRONG APP_ID. TRY AGAIN\n");
                        ble_send_event_secure(COMMAND_NACK_AUTH);
                        return;
                    }
                }
            } else if(event == EV_BLE_DISCONNECTED){
                if(is_app_id_empty()){
                    current_state = STATE_UNPAIRED;
                    LOG_INF("TIMEOUT/DISCONNECTION/RESET WITHOUT CLAIMED. FROM: STATE_SETUP_WAITING_IDENTIFIER TO STATE_UNPAIRED\n");
                } else {
                    current_state = STATE_BONDED_DISCONNECTED;
                    ble_start_reconnect_advertising();
                    LOG_INF("FROM: STATE_SETUP_WAITING_IDENTIFIER TO: STATE_BONDED_DISCONNECTED (reconnecting...)\n");
                }
            }
            break;

        case STATE_AUTHENTICATED:
            // On this state, the device is connected and authenticated with the app. It can send and receive events.
            if (event == EV_APP_FRIEND_EMERGENCY){
                haptics_play(HAPTICS_PATTERN_FRIEND_EMERGENCY);
                LOG_INF("FROM: STATE_AUTHENTICATED TO: STATE_AUTHENTICATED (friend emergency)\n");
            } else if (event == EV_BLE_DISCONNECTED) {
                current_state = STATE_BONDED_DISCONNECTED;
                ble_start_reconnect_advertising();
                LOG_INF("FROM: STATE_AUTHENTICATED TO: STATE_BONDED_DISCONNECTED\n");

            } else if (event == EV_BTN_1_PULSE) {
                haptics_play(HAPTICS_PATTERN_ROUTINE_START);
                ble_send_event_secure(COMMAND_ROUTINE);
                LOG_INF("FROM: STATE_AUTHENTICATED TO: STATE_AUTHENTICATED (routine command sent)\n");

            } else if (event == EV_BTN_LONG_PRESS) {
                haptics_play(HAPTICS_PATTERN_ROUTINE_CANCEL);
                ble_send_event_secure(COMMAND_END_ROUTINE);
                LOG_INF("FROM: STATE_AUTHENTICATED TO: STATE_AUTHENTICATED (routine end command sent)\n");

            }
            break;

        case STATE_BONDED_DISCONNECTED:
            if (event == EV_BLE_CONNECTED) {
                LOG_INF("FROM: STATE_BONDED_DISCONNECTED TO: STATE_WAITING_NOTIFICATION_ENABLE\n");
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
        LOG_INF("FROM: %d TO: STATE_UNPAIRED\n", current_state);
        current_state = STATE_UNPAIRED;
        ble_disconnect();
        storage_factory_reset();
        haptics_play_effect(HAPTICS_EFFECT_RESET);
        return;
    }
}