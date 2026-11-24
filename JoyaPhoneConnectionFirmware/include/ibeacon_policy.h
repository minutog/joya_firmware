#ifndef IBEACON_POLICY_H
#define IBEACON_POLICY_H

#include <stdbool.h>
#include "emergency_phase.h"

typedef enum {
    BLE_IBEACON_OFF,
    BLE_IBEACON_CONNECTION,
    BLE_IBEACON_EMERGENCY,
} ble_ibeacon_mode_t;

static inline ble_ibeacon_mode_t ibeacon_mode_for_state(bool has_app_id,
                                                        emergency_phase_t phase)
{
    if (!has_app_id) {
        return BLE_IBEACON_OFF;
    }
    return emergency_phase_needs_notification(phase) ? BLE_IBEACON_EMERGENCY :
                                                       BLE_IBEACON_CONNECTION;
}

#endif
