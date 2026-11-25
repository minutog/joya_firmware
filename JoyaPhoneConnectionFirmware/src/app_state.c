#include "app_state.h"
#include "battery.h"
#include "ota_access.h"
#include "diagnostics.h"
#include "wdt_guard.h"
#include "pending_routine.h"
#include <errno.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

/** @brief Array of retry intervals for emergency events in milliseconds */
const uint32_t EMERGENCY_RETRY_MS[] = {EMERGENCY_RETRY_INITIAL_MS, EMERGENCY_RETRY_FAST_MS, EMERGENCY_RETRY_MEDIUM_MS, EMERGENCY_RETRY_SLOW_MS, EMERGENCY_RETRY_VERY_SLOW_MS};
/** @brief Current index for emergency retry intervals */
uint8_t current_retry_index = 0;

static struct k_work_delayable connection_timeout_work;
static struct k_work_delayable emergency_retry_work;
static struct k_work_delayable advertising_restart_work;
static struct k_work_delayable critical_window_work;
#define CRITICAL_WINDOW_MS (5 * 60 * 1000)
static uint8_t advertising_attempts;
K_SEM_DEFINE(test_setup_sem, 0, 1);
static int test_setup_result;

/** @brief Current application state */
static volatile app_state_t current_state = STATE_UNPAIRED;
/** @brief Message queue for events */
K_MSGQ_DEFINE(event_queue, sizeof(app_event_t), 20, 4);

unsigned int app_state_pending_events(void)
{
    return k_msgq_num_used_get(&event_queue);
}

/** @brief Thread for the finite state machine */
K_THREAD_DEFINE(fsm_thread_id, 4098, fsm_thread_loop, NULL, NULL, NULL, 5, 0, 0);

static bool emergency_alerts_active = false;
static void sync_ibeacon_mode(void);
static bool reboot_pending;
static bool suspended_idle;
static bool critical_idle;
static bool critical_window_open;
static bool boot_battery_seen;
static bool boot_reconnect_pending;
static int64_t critical_window_deadline_ms;
static int battery_level = -1;

static void notify_current_battery_level(void)
{
    if (battery_level >= 0 && current_state == STATE_AUTHENTICATED) {
        (void)ble_send_battery_change((uint8_t)battery_level);
    }
}

static bool hold_blocked;
static bool hold_released;
static bool hold_pattern_done;
static enum {
    HOLD_NONE,
    HOLD_SUSPEND,
    HOLD_FACTORY_RESET,
} hold_action;

static void suspend_pattern_finished(void)
{
    (void)add_event(EV_HAPTICS_SUSPEND_DONE);
}

static void factory_pattern_finished(void)
{
    (void)add_event(EV_HAPTICS_FACTORY_DONE);
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

static void factory_reset_and_reboot(void)
{
    reboot_pending = true;
    (void)k_work_cancel_delayable(&advertising_restart_work);
    current_state = STATE_UNPAIRED;
    (void)ble_disconnect();
    emergency_stop_alerts();
    if (storage_factory_reset() != 0) {
        diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
        /* Keep the RAM reset and stay idle so the gesture can be retried.
         * Rebooting here could restore an APP_ID whose deletion failed.
         */
        suspended_idle = true;
        reboot_pending = false;
        sync_ibeacon_mode();
        return;
    }
    sync_ibeacon_mode();
    sys_reboot(SYS_REBOOT_COLD);
}

static void enter_unpaired_idle(void)
{
    (void)k_work_cancel_delayable(&advertising_restart_work);
    k_work_cancel_delayable(&connection_timeout_work);
    (void)k_work_cancel_delayable(&critical_window_work);
    critical_window_open = false;
    emergency_stop_alerts();
    ota_access_disable();
    suspended_idle = true;
    current_state = STATE_UNPAIRED;
    (void)ble_disconnect();
}

static void enter_critical_idle(void)
{
    critical_idle = true;
    (void)k_work_cancel_delayable(&critical_window_work);
    critical_window_open = false;
    if (hold_action == HOLD_SUSPEND) {
        hold_action = HOLD_NONE;
        hold_released = false;
        hold_pattern_done = false;
    }
    if (!suspended_idle) {
        enter_unpaired_idle();
    }
}

static void sync_ibeacon_mode(void)
{
    ble_ibeacon_mode_t mode = ibeacon_mode_for_state(!is_app_id_empty());
    (void)ble_set_ibeacon_mode(mode);
}

static int start_advertising_with_ibeacon(bool is_new_connection)
{
    int err = ble_start_setup_advertising(is_new_connection);
    if (err == 0) {
        sync_ibeacon_mode();
    }
    return err;
}

static void start_setup_from_idle(void)
{
    int ret = start_advertising_with_ibeacon(is_app_id_empty());
    if (ret == 0) {
        suspended_idle = false;
        current_state = STATE_SETUP_MODE;
        haptics_play(HAPTICS_PATTERN_SETUP_MODE);
        (void)k_work_reschedule(&connection_timeout_work,
                                K_MSEC(BLE_SETUP_TIMEOUT_MS));
    }
}

static void start_critical_window(void)
{
    if (ble_has_connection()) {
        return;
    }
    if (start_advertising_with_ibeacon(is_app_id_empty()) != 0) {
        return;
    }
    (void)k_work_cancel_delayable(&connection_timeout_work);
    suspended_idle = false;
    current_state = STATE_SETUP_MODE;
    critical_window_open = true;
    critical_window_deadline_ms = k_uptime_get() + CRITICAL_WINDOW_MS;
    if (k_work_reschedule(&critical_window_work,
                          K_MSEC(CRITICAL_WINDOW_MS)) < 0) {
        enter_critical_idle();
    }
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
    if (!emergency_phase_needs_notification(storage_get_emergency_phase())) {
        emergency_stop_alerts();
        return;
    }
    emergency_alerts_active = true;

    k_work_cancel_delayable(&emergency_retry_work);
    reset_emergency_retry_index();

    if (current_state != STATE_AUTHENTICATED) {
        return;
    }

    // LOG: Sending emergency event
    ble_send_event_authenticated(COMMAND_EMERGENCY);
    emergency_schedule_next_retry();
}

static void send_pending_routine(void)
{
    uint8_t command = storage_get_pending_routine();
    if (command == 0 || current_state != STATE_AUTHENTICATED ||
        is_in_emergency()) {
        return;
    }
    if (ble_send_event_authenticated(command) == 0) {
        if (storage_save_pending_routine(0) != 0) {
            diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
        }
    }
}

static void send_pending_after_auth(void)
{
    if (emergency_phase_needs_notification(storage_get_emergency_phase())) {
        emergency_restart_alerts();
        haptics_play(HAPTICS_PATTERN_EMERGENCY_START);
    } else {
        send_pending_routine();
    }
}

static void handle_routine_button(uint8_t command)
{
    if (pending_routine_next(storage_get_pending_routine(), command,
                             !is_app_id_empty(), is_in_emergency()) != command) {
        return;
    }
    haptics_play(command == COMMAND_ROUTINE ? HAPTICS_PATTERN_ROUTINE_START :
                                            HAPTICS_PATTERN_ROUTINE_CANCEL);
    if (current_state == STATE_AUTHENTICATED &&
        ble_send_event_authenticated(command) == 0) {
        if (storage_save_pending_routine(0) != 0) {
            diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
        }
    } else if (storage_save_pending_routine(command) != 0) {
        diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
    }
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
    add_event(EV_BLE_TIMEOUT);
}

static void advertising_restart_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    int err = add_event(EV_BLE_RESTART_ADVERTISING);
    if (err != 0) {
        return;
    }
}

static void critical_window_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    if (add_event(EV_CRITICAL_WINDOW_TIMEOUT) != 0) {
        (void)k_work_reschedule(&critical_window_work, K_SECONDS(1));
    }
}

static void schedule_advertising_restart(void)
{
    advertising_attempts = 0;
    /* Run after the disconnect callback and its queued conn reference are released. */
    int ret = k_work_reschedule(&advertising_restart_work, K_MSEC(10));
    if (ret < 0) {
        return;
    }
}


/**
 * PUBLIC API
 */

app_state_t get_current_state(void) {
    return current_state;
}

int app_start_test_setup(void)
{
    int err = add_event(EV_BOOT_TEST_SETUP_REQUEST);
    if (err != 0) {
        return err;
    }
    err = k_sem_take(&test_setup_sem, K_SECONDS(10));
    return err == 0 ? test_setup_result : err;
}


int add_event(event_type_t event)
{
    return add_event_for_conn(event, NULL, NULL);
}

int add_event_for_conn(event_type_t event, struct bt_conn *conn, const uint8_t *app_id)
{
    app_event_t entry = {.type = event, .conn = conn ? bt_conn_ref(conn) : NULL};
    if (app_id) {
        memcpy(entry.app_id, app_id, SIZE_APP_ID);
    }
    int err = k_msgq_put(&event_queue, &entry, K_NO_WAIT);
    if (err) {
        diagnostics_report_error(DIAG_ERROR_EVENT_QUEUE_FULL);
    }
    if (err && entry.conn) {
        bt_conn_unref(entry.conn);
    }
    return err;
}

void fsm_thread_loop(void) {
    k_work_init_delayable(&emergency_retry_work, emergency_retry_work_handler);
    k_work_init_delayable(&connection_timeout_work, connection_timeout_work_handler);
    k_work_init_delayable(&advertising_restart_work, advertising_restart_work_handler);
    k_work_init_delayable(&critical_window_work, critical_window_work_handler);

    app_event_t entry;
    while (1) {
        k_msgq_get(&event_queue, &entry, K_FOREVER);
        wdt_guard_fsm_event_begin();
        process_event(&entry);
        wdt_guard_fsm_event_end();
        if (entry.conn) {
            bt_conn_unref(entry.conn);
        }
    }
}

/*
 * Button gestures are detected only after firmware initialization.
 * A button press already active during boot is not considered a valid gesture.
 */
void process_event(const app_event_t *entry) {
    event_type_t event = entry->type;
    int ret;

    if (reboot_pending) {
        return;
    }

    if (event == EV_BLE_CONNECTED && !ble_is_current_conn(entry->conn)) {
        return;
    }
    if (event == EV_BLE_DISCONNECTED && ble_has_connection()) {
        return; /* A prior connection disconnected after a new one arrived. */
    }
    if (event == EV_BLE_NOTIFY_ENABLED &&
        !ble_commands_subscribed_for_conn(entry->conn)) {
        return;
    }
    if ((event == EV_APP_ACK_EMERGENCY || event == EV_APP_STOP_EMERGENCY ||
         event == EV_APP_FOLLOW_ME || event == EV_APP_FRIEND_EMERGENCY) &&
        !ble_is_authenticated_conn(entry->conn)) {
        return;
    }

    if (event == EV_BOOT_TEST_SETUP_REQUEST) {
        if (current_state != STATE_UNPAIRED) {
            test_setup_result = -EBUSY;
        } else {
            /* Keep Test advertising available until connection or rollback. */
            test_setup_result = start_advertising_with_ibeacon(true);
            if (test_setup_result == 0) {
                current_state = STATE_SETUP_MODE;
                /* A Test image needs time to accept CONFIRM even when the
                 * ADC subsequently reports level 2 or never gives a level.
                 */
                critical_window_open = true;
                critical_window_deadline_ms = k_uptime_get() + CRITICAL_WINDOW_MS;
                if (k_work_reschedule(&critical_window_work,
                                      K_MSEC(CRITICAL_WINDOW_MS)) < 0) {
                    test_setup_result = -EIO;
                }
            }
        }
        k_sem_give(&test_setup_sem);
        return;
    }

    if (event == EV_BATTERY_LEVEL_1 || event == EV_BATTERY_LEVEL_2 ||
        event == EV_BATTERY_NORMAL) {
        battery_level = event == EV_BATTERY_LEVEL_2 ? 2 :
                        event == EV_BATTERY_LEVEL_1 ? 1 : 0;
        if (!boot_battery_seen) {
            boot_battery_seen = true;
            boot_reconnect_pending = !is_app_id_empty() &&
                                     boot_is_img_confirmed();
        }
        if (battery_level != 2) {
            if (critical_window_open) {
                (void)k_work_cancel_delayable(&critical_window_work);
                critical_window_open = false;
                if (current_state == STATE_SETUP_MODE &&
                    !ble_has_connection() && boot_is_img_confirmed()) {
                    (void)k_work_reschedule(&connection_timeout_work,
                                            K_MSEC(BLE_SETUP_TIMEOUT_MS));
                }
            }
            if (!suspended_idle) {
                critical_idle = false;
            }
        }
        if (battery_level == 2 && critical_window_open) {
            critical_idle = true;
        }
        haptics_set_allowed(battery_level == 0 && !critical_idle);
        notify_current_battery_level();
        if (battery_level == 2) {
            /* Keep an active flash transfer alive; close OTA on its stop or disconnect. */
            bool updating = ota_access_is_update_active();
            if (!critical_window_open) {
                ota_access_disable();
            }
            if (!updating && !critical_window_open) {
                enter_critical_idle();
            }
        } else if (boot_reconnect_pending && !ble_has_connection() &&
                   (current_state == STATE_UNPAIRED ||
                    current_state == STATE_BONDED_DISCONNECTED)) {
            suspended_idle = false; /* A boot-time critical idle has recovered. */
            critical_idle = false;
            haptics_set_allowed(battery_level == 0);
            current_state = STATE_BONDED_DISCONNECTED;
            ret = start_advertising_with_ibeacon(false);
            if (ret == 0) {
                boot_reconnect_pending = false;
            } else {
                schedule_advertising_restart();
            }
        }
        return;
    }

    if (event == EV_BATTERY_RECHECK_DONE) {
        return;
    }

    if (event == EV_BLE_BATTERY_NOTIFY_ENABLED) {
        notify_current_battery_level();
        return;
    }

    if (event == EV_BLE_NOTIFY_ENABLED &&
        current_state == STATE_AUTHENTICATED) {
        send_pending_after_auth();
        return;
    }

    if (event == EV_CRITICAL_WINDOW_TIMEOUT) {
        if (!critical_window_open ||
            k_uptime_get() < critical_window_deadline_ms) {
            return;
        }
        critical_window_open = false;
        if (battery_level == 2 && !ota_access_is_update_active()) {
            enter_critical_idle();
        }
        return;
    }

    if (event == EV_OTA_UPDATE_STOPPED) {
        if (battery_level == 2 && !critical_window_open &&
            !ota_access_is_update_active() &&
            !suspended_idle) {
            ota_access_disable();
            enter_critical_idle();
        }
        return;
    }

    if (event == EV_BTN_2_PULSE && ble_has_connection()) {
        return;
    }

    if (event == EV_BTN_2_PULSE && !suspended_idle &&
        battery_level == 2 && !critical_window_open) {
        enter_critical_idle();
        start_critical_window();
        return;
    }

    if (event == EV_BTN_2_PULSE && suspended_idle && critical_idle) {
        if (battery_level == 0 || battery_level == 1) {
            start_setup_from_idle();
            if (!suspended_idle) {
                critical_idle = false;
                haptics_set_allowed(battery_level == 0);
            }
        } else {
            start_critical_window();
        }
        return;
    }

    if (event == EV_BTN_2_PULSE && !suspended_idle &&
        (current_state == STATE_WAITING_NOTIFICATION_ENABLE ||
         current_state == STATE_SETUP_WAITING_IDENTIFIER ||
         current_state == STATE_SETUP_WAITING_NEW_IDENTIFIER ||
         current_state == STATE_AUTHENTICATED ||
         current_state == STATE_EMERGENCY)) {
        emergency_stop_alerts();
        /* The BLE disconnect callback already cleared its connection,
         * authentication and SMP context; only the queued FSM event was lost.
         */
        current_state = is_app_id_empty() ? STATE_UNPAIRED :
                                            STATE_BONDED_DISCONNECTED;
    }

    if (event == EV_BTN_2_PULSE && critical_window_open && !suspended_idle) {
        if (start_advertising_with_ibeacon(is_app_id_empty()) == 0) {
            current_state = is_app_id_empty() ? STATE_SETUP_MODE :
                                                STATE_BONDED_DISCONNECTED;
        }
        return;
    }

    if (suspended_idle && event != EV_BTN_2_PULSE &&
        event != EV_BTN_FACTORY_THRESHOLD &&
        event != EV_BTN_FACTORY_RESET &&
        event != EV_HAPTICS_FACTORY_DONE) {
        return;
    }

    if (event == EV_BLE_CONNECTED) {
        boot_reconnect_pending = false;
        (void)k_work_cancel_delayable(&advertising_restart_work);
        (void)ble_set_ibeacon_mode(BLE_IBEACON_OFF);
    }

    if (event == EV_BLE_RESTART_ADVERTISING) {
        if (reboot_pending || suspended_idle || advertising_attempts >= 3 ||
            ble_has_connection() ||
            !((current_state == STATE_BONDED_DISCONNECTED && !is_app_id_empty()) ||
              (current_state == STATE_SETUP_MODE && is_app_id_empty()))) {
            return;
        }

        advertising_attempts++;
        ret = start_advertising_with_ibeacon(is_app_id_empty());
        if (ret != 0) {
            if (advertising_attempts < 3 &&
                (ret == -ENOMEM || ret == -EBUSY || ret == -EAGAIN)) {
                int work_ret = k_work_reschedule(&advertising_restart_work,
                                                 K_SECONDS(5));
                if (work_ret < 0) {
                    return;
                }
            }
        }
        return;
    }

    if (event == EV_BLE_DISCONNECTED) {
        if (critical_window_open) {
            current_state = is_app_id_empty() ? STATE_SETUP_MODE :
                                                STATE_BONDED_DISCONNECTED;
            schedule_advertising_restart();
        } else if (battery_level == 2 && !ota_access_is_update_active()) {
            enter_critical_idle();
        } else if (is_in_emergency()) {
            emergency_stop_alerts();
            current_state = is_app_id_empty() ? STATE_SETUP_MODE :
                                                STATE_BONDED_DISCONNECTED;
            schedule_advertising_restart();
        } else if (current_state == STATE_SETUP_MODE) {
            (void)k_work_cancel_delayable(&connection_timeout_work);
            current_state = STATE_UNPAIRED;
            ret = ble_stop_advertising();
            if (ret != 0) {
                return;
            }
        } else if (current_state == STATE_WAITING_NOTIFICATION_ENABLE ||
                   current_state == STATE_SETUP_WAITING_IDENTIFIER ||
                   current_state == STATE_AUTHENTICATED) {
            current_state = is_app_id_empty() ? STATE_UNPAIRED :
                                                STATE_BONDED_DISCONNECTED;
            if (current_state == STATE_BONDED_DISCONNECTED) {
                schedule_advertising_restart();
            }
        }
        return;
    }

    if (hold_blocked && event == EV_BTN_FACTORY_RESET) {
        hold_blocked = false;
        return;
    }

    if (event == EV_BTN_SUSPEND_REQUEST) {
        /* A short hold enters the idle state only after release and haptic feedback. */
        if (ota_access_is_update_active()) {
            return;
        }
        boot_reconnect_pending = false;
        hold_action = HOLD_SUSPEND;
        hold_released = true;
        hold_pattern_done = false;
        ret = haptics_play_with_completion(HAPTICS_PATTERN_SUSPEND,
                                           suspend_pattern_finished);
        if (ret != 0) {
            enter_unpaired_idle();
            hold_action = HOLD_NONE;
            hold_released = false;
        }
        return;
    }

    if (event == EV_BTN_FACTORY_THRESHOLD) {
        /* Factory reset remains a deliberate 15-second recovery gesture. */
        if (ota_access_is_update_active()) {
            hold_action = HOLD_NONE;
            hold_blocked = true;
            return;
        }
        hold_blocked = false;
        hold_action = HOLD_FACTORY_RESET;
        hold_released = false;
        hold_pattern_done = false;
        haptics_stop();
        ret = haptics_play_with_completion(HAPTICS_PATTERN_FACTORY_RESET,
                                           factory_pattern_finished);
        if (ret != 0) {
            hold_pattern_done = true;
        }
        return;
    }

    if (event == EV_HAPTICS_SUSPEND_DONE && hold_action == HOLD_SUSPEND) {
        hold_pattern_done = true;
    } else if (event == EV_HAPTICS_FACTORY_DONE &&
               hold_action == HOLD_FACTORY_RESET) {
        hold_pattern_done = true;
    } else if (event == EV_BTN_FACTORY_RESET &&
               hold_action == HOLD_FACTORY_RESET) {
        hold_released = true;
    }

    if (hold_released && hold_pattern_done) {
        if (ota_access_is_update_active()) {
            hold_action = HOLD_NONE;
            hold_released = false;
            return;
        }
        if (hold_action == HOLD_FACTORY_RESET) {
            factory_reset_and_reboot();
        } else if (hold_action == HOLD_SUSPEND) {
            enter_unpaired_idle();
        }
        hold_action = HOLD_NONE;
        hold_released = false;
    }
    if (event == EV_HAPTICS_SUSPEND_DONE ||
        event == EV_HAPTICS_FACTORY_DONE ||
        event == EV_BTN_FACTORY_RESET) {
        return;
    }

    if (event == EV_APP_OTA_ENABLE || event == EV_APP_OTA_DISABLE) {
        if (current_state == STATE_AUTHENTICATED &&
            ble_is_authenticated_conn(entry->conn)) {
            if (event == EV_APP_OTA_ENABLE) {
                (void)ota_access_enable();
            } else {
                ota_access_disable();
            }
        }
        return;
    }

    if (event == EV_BTN_EMERGENCY) {
        if (emergency_phase_needs_notification(storage_get_emergency_phase())) {
            haptics_play(HAPTICS_PATTERN_EMERGENCY_START);
            return; /* The same alert is already awaiting its ACK. */
        }
        // LOG: Emergency button pressed
        if (!is_app_id_empty()) {
            /* Commit the routine tombstone first. Older firmware after an OTA
             * rollback cannot resurrect a routine discarded for this alert.
             */
            if (storage_save_pending_routine(0) != 0) {
                diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
            }
            ret = storage_save_emergency_phase(
                emergency_phase_transition(storage_get_emergency_phase(),
                                           EMERGENCY_TRIGGER));
            if (ret != 0) {
                diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
            }
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
            start_advertising_with_ibeacon(false);
        } else if (current_state == STATE_UNPAIRED && !is_app_id_empty()) {
            current_state = STATE_BONDED_DISCONNECTED;
            start_advertising_with_ibeacon(false);
        } else if (current_state == STATE_UNPAIRED && is_app_id_empty()) {
            current_state = STATE_SETUP_MODE;
            start_advertising_with_ibeacon(true);
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
                if (!emergency_phase_needs_notification(storage_get_emergency_phase())) {
                    return;
                }

                if (!emergency_alerts_active) {
                    return;
                }

                if (current_state != STATE_AUTHENTICATED) {
                    return;
                }

                // LOG: Retrying emergency event

                ret = ble_send_event_authenticated(COMMAND_EMERGENCY);
                if (ret != 0) {
                    // LOG: Failed to resend COMMAND_EMERGENCY - continuing
                }

                emergency_schedule_next_retry();
                return;

            case EV_APP_FOLLOW_ME:
                if (current_state != STATE_AUTHENTICATED) {
                    return;
                }
                ret = ble_send_event_authenticated(COMMAND_ACK);
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
                ret = storage_save_emergency_phase(
                    emergency_phase_transition(storage_get_emergency_phase(),
                                               EMERGENCY_ACK));
                if (ret == 1) {
                    // LOG: Failed to persist ACK - continuing with RAM state
                }
                ret = ble_send_event_authenticated(COMMAND_ACK);
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
                if (storage_save_pending_routine(0) != 0) {
                    diagnostics_report_error(DIAG_ERROR_FLASH_WRITE);
                }
                ret = storage_save_emergency_phase(
                    emergency_phase_transition(storage_get_emergency_phase(),
                                               EMERGENCY_STOP));
                if (ret == 1) {
                    // LOG: Failed to save emergency state in flash - continuing without saving
                }
                ret = ble_send_event_authenticated(COMMAND_ACK);
                if(ret != 0){
                    // LOG: Failed to send ACK - continuing without sending
                }
                return;

            case EV_BLE_TIMEOUT:
                if (critical_window_open) {
                    return;
                }
                // LOG: BLE timeout
                if (is_app_id_empty()) {
                    // LOG: No APP_ID stored - entering setup mode (starting ADV)
                    current_state = STATE_SETUP_MODE;
                    start_advertising_with_ibeacon(true);
                } else {
                    // LOG: APP_ID stored - entering bonded disconnected state (starting ADV)
                    current_state = STATE_BONDED_DISCONNECTED;
                    start_advertising_with_ibeacon(false);
                }

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

    if (event == EV_BTN_1_PULSE || event == EV_BTN_LONG_PRESS) {
        handle_routine_button(event == EV_BTN_1_PULSE ? COMMAND_ROUTINE :
                                                        COMMAND_END_ROUTINE);
        return;
    }

    switch (current_state) {
        case STATE_UNPAIRED:
            if (event == EV_BTN_2_PULSE) {
                // LOG: Entering setup mode (starting ADV)
                start_setup_from_idle();
            }
            break;

        case STATE_SETUP_MODE:
            if (event == EV_BLE_CONNECTED) {
                // LOG: BLE connected - waiting for notification enable
                k_work_cancel_delayable(&connection_timeout_work);
                current_state = STATE_WAITING_NOTIFICATION_ENABLE;
                ble_stop_advertising();

            } else if (event == EV_BLE_TIMEOUT && !critical_window_open) {
                // LOG: BLE setup timeout in setup mode - returning to unpaired state
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
            }
            break;

        case STATE_SETUP_WAITING_IDENTIFIER:
            if(event == EV_APP_IDENTIFIER_RECEIVED){
                // LOG: APP_ID received - checking validity
                if(is_app_id_empty()){
                    // LOG: No APP_ID stored - saving new APP_ID
                    // New APP_ID
                    if (!ble_is_current_conn(entry->conn)) {
                        return;
                    }
                    ret = save_received_app_id(entry->app_id);
                    if (is_app_id_empty()) {
                        /* A stale routine could not be cleared. Retry setup. */
                        return;
                    }
                    sync_ibeacon_mode();
                    
                    // LOG: New APP_ID saved - sending ACK
                    current_state = STATE_AUTHENTICATED;
                    if (!ble_mark_authenticated_conn(entry->conn)) {
                        return;
                    }
                    // It is safe to send because it is already authenticated
                    ret = ble_send_event_for_conn(entry->conn, COMMAND_ACK);
                    if(ret != 0){
                        // LOG: Failed to send ACK - continuing without sending
                    }
                    notify_current_battery_level();

                    haptics_play(HAPTICS_PATTERN_ACK_CONNECTION);
                    send_pending_after_auth();
                    return;
                    
                } else {
                    // LOG: APP_ID already stored - checking if it matches received APP_ID
                    // Already have an APP_ID
                    if (!ble_is_current_conn(entry->conn)) {
                        return;
                    }
                    if(check_app_id(entry->app_id, storage_get_app_id()) == 0){
                        // LOG: APP_ID matches stored APP_ID - sending ACK
                        current_state = STATE_AUTHENTICATED;
                        if (!ble_mark_authenticated_conn(entry->conn)) {
                            return;
                        }
                        ret = ble_send_event_for_conn(entry->conn, COMMAND_ACK);
                        if(ret != 0){
                            // LOG: Failed to send ACK - continuing without sending
                        }
                        notify_current_battery_level();
                        send_pending_after_auth();
                        haptics_play(HAPTICS_PATTERN_ACK_CONNECTION);
                        return;
                    } else {
                        // LOG: APP_ID does not match stored APP_ID - sending NACK
                        // (improvement) max retry attempts? or just wait for the next connection?
                        ret = ble_send_event_for_conn(entry->conn, COMMAND_NACK);
                        if(ret != 0){
                            // LOG: Failed to send NACK - continuing without sending
                        }
                        return;
                    }
                }
            }
            break;

        case STATE_AUTHENTICATED:
            // On this state, the device is connected and authenticated with the app. It can send and receive events.
            if (event == EV_APP_FRIEND_EMERGENCY){
                // LOG: Friend emergency event received
                haptics_play(HAPTICS_PATTERN_FRIEND_EMERGENCY);
                ret = ble_send_event_authenticated(COMMAND_ACK);
                if(ret != 0){
                    // LOG: Failed to send ACK - continuing without sending
                }
            }
            break;

        case STATE_BONDED_DISCONNECTED:
            if (event == EV_BLE_CONNECTED) {
                // LOG: BLE connected - waiting for notification enable
                current_state = STATE_WAITING_NOTIFICATION_ENABLE;
            } else if (event == EV_BTN_2_PULSE) {
                /* Let the user retry advertising if the restart after disconnect failed. */
                ret = start_advertising_with_ibeacon(false);
                if (ret == 0) {
                    (void)k_work_cancel_delayable(&advertising_restart_work);
                    haptics_play(HAPTICS_PATTERN_SETUP_MODE);
                }
            } 
            break;

        default:
            // (for future use) Handle unexpected state
            break;
    }
}
