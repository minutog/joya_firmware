#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>

typedef enum {
    STATE_UNPAIRED,
    STATE_SETUP_MODE,
    STATE_BONDED_DISCONNECTED,
    STATE_CONNECTED,
    STATE_EMERGENCY,
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

    // APPLICATION EVENTS
    EV_APP_CMD_STOP_EMERGENCY,     
    EV_APP_CMD_FOLLOW_ME
} event_type_t;

void process_event(event_type_t event);
app_state_t get_current_state(void);

#endif // APP_STATE_H