#ifndef IBEACON_POLICY_H
#define IBEACON_POLICY_H

#include <stdbool.h>

typedef enum {
    BLE_IBEACON_OFF,
    BLE_IBEACON_CONNECTION,
} ble_ibeacon_mode_t;

static inline ble_ibeacon_mode_t ibeacon_mode_for_state(bool has_app_id)
{
    return has_app_id ? BLE_IBEACON_CONNECTION : BLE_IBEACON_OFF;
}

#endif
