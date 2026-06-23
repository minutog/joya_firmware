#ifndef APP_STATE_H
#define APP_STATE_H

#include "ble_driver.h"
#include "app_comm.h"
#include "flash_memory.h"

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#define EMERGENCY_RETRY_INITIAL_MS 1000
#define EMERGENCY_RETRY_FAST_MS 2000
#define EMERGENCY_RETRY_MEDIUM_MS 5000
#define EMERGENCY_RETRY_SLOW_MS 10000
#define EMERGENCY_RETRY_VERY_SLOW_MS 30000

#define BLE_SETUP_TIMEOUT_MS 90000
#define BLE_SETUP_WAITING_IDENTIFIER_TIMEOUT_MS 30000

typedef enum {
    STATE_UNPAIRED,
    STATE_SETUP_MODE,
    STATE_SETUP_WAITING_IDENTIFIER,
    STATE_SETUP_WAITING_NEW_IDENTIFIER,
    STATE_WAITING_NOTIFICATION_ENABLE,
    STATE_BONDED_DISCONNECTED,
    STATE_AUTHENTICATED,
    STATE_EMERGENCY
} app_state_t;

typedef enum {
    // BUTTON EVENTS
    EV_BTN_1_PULSE,
    EV_BTN_2_PULSE,
    EV_BTN_LONG_PRESS,  
    EV_BTN_EMERGENCY,   
    EV_BTN_FACTORY_RESET,

    // JOYA EVENTS
    EV_APP_IDENTIFIER_RECEIVED,
    
    // CONNECTION EVENTS
    EV_BLE_CONNECTED,
    EV_BLE_DISCONNECTED,
    EV_BLE_TIMEOUT,
    EV_BLE_NOTIFY_ENABLED,
    EV_APP_AUTHENTICATED,

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