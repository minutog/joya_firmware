#ifndef APP_STATE_H
#define APP_STATE_H

#include "ble_driver.h"
#include "app_comm.h"

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#define BLE_SETUP_TIMEOUT_MS 90000

typedef enum {
    STATE_UNPAIRED,
    STATE_SETUP_MODE,
    STATE_BONDED_DISCONNECTED,
    STATE_CONNECTED,
    STATE_EMERGENCY
} app_state_t;

typedef enum {
    // BUTTON EVENTS
    EV_BTN_1_PULSE,
    EV_BTN_2_PULSE,
    EV_BTN_LONG_PRESS,  
    EV_BTN_EMERGENCY,   
    EV_BTN_FACTORY_RESET,

    // CONNECTION EVENTS
    EV_BLE_CONNECTED,
    EV_BLE_DISCONNECTED,
    EV_BLE_TIMEOUT,
    EV_BLE_NOTIFY_ENABLED,
    EV_BLE_NOTIFY_DISABLED,

    // APPLICATION EVENTS
    EV_APP_CMD_STOP_EMERGENCY,     
    EV_APP_CMD_FOLLOW_ME,
    EV_APP_ACK_EMERGENCY
} event_type_t;

/** @brief Process an event
 * @param event The event to process
 */
void process_event(event_type_t event);

/** @brief Main loop for the finite state machine
 * This function initializes the work structures and enters the main event loop.
 */
void fsm_thread_loop(void);

/** @brief Schedule a retry for the emergency command
 * This function reschedules the emergency retry work based on the current retry index.
 */
void scheduler_retry(void);

/** @brief Add an event to the event queue
 * @param event The event to add
 * @return 0 on success, -1 on failure
 */
int add_event(event_type_t event);

/** @brief Get the current application state
 * @return The current application state
 * 
 */
app_state_t get_current_state(void);

#endif // APP_STATE_H