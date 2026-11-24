#include "emergency_phase.h"
#include "ibeacon_policy.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    emergency_phase_t phase = EMERGENCY_NONE;

    /* Triple click starts an alert, including after a previous ACK. */
    phase = emergency_phase_transition(phase, EMERGENCY_TRIGGER);
    assert(phase == EMERGENCY_PENDING_ACK);
    assert(emergency_phase_needs_notification(phase));

    /* Disconnect leaves the persisted phase untouched; reconnect resumes 0x04. */
    emergency_phase_t after_disconnect = phase;
    assert(emergency_phase_needs_notification(after_disconnect));
    uint8_t stored = (uint8_t)after_disconnect;
    phase = EMERGENCY_NONE;
    assert(emergency_phase_decode(stored, &phase));
    assert(phase == EMERGENCY_PENDING_ACK);

    phase = emergency_phase_transition(phase, EMERGENCY_ACK);
    assert(phase == EMERGENCY_ACKED);
    assert(!emergency_phase_needs_notification(phase));
    assert(phase != EMERGENCY_NONE);

    /* Loading the saved byte after a reboot must preserve the ACK. */
    stored = (uint8_t)phase;
    phase = EMERGENCY_NONE;
    assert(emergency_phase_decode(stored, &phase));
    assert(phase == EMERGENCY_ACKED);
    assert(!emergency_phase_needs_notification(phase));

    phase = emergency_phase_transition(phase, EMERGENCY_STOP);
    assert(phase == EMERGENCY_NONE);
    assert(!emergency_phase_needs_notification(phase));
    stored = (uint8_t)phase;
    assert(emergency_phase_decode(stored, &phase) && phase == EMERGENCY_NONE);
    assert(emergency_phase_transition(phase, EMERGENCY_ACK) == EMERGENCY_NONE);

    /* Older firmware stored a one-byte bool at joya/emergency. */
    assert(emergency_phase_decode(0, &phase) && phase == EMERGENCY_NONE);
    assert(emergency_phase_decode(1, &phase) && phase == EMERGENCY_PENDING_ACK);
    assert(emergency_phase_needs_notification(phase));
    assert(emergency_phase_decode(2, &phase) && phase == EMERGENCY_ACKED);
    assert(!emergency_phase_decode(3, &phase));
    assert(phase == EMERGENCY_ACKED);
    assert(!emergency_phase_decode(1, NULL));

    phase = emergency_phase_transition(EMERGENCY_ACKED, EMERGENCY_TRIGGER);
    assert(phase == EMERGENCY_PENDING_ACK);

    /* Initial Setup has no beacon, even when an emergency is pending. */
    assert(ibeacon_mode_for_state(false, EMERGENCY_NONE) == BLE_IBEACON_OFF);
    assert(ibeacon_mode_for_state(false, EMERGENCY_PENDING_ACK) == BLE_IBEACON_OFF);
    assert(ibeacon_mode_for_state(false, EMERGENCY_ACKED) == BLE_IBEACON_OFF);

    /* Registration and factory reset change eligibility during execution. */
    bool has_app_id = false;
    assert(ibeacon_mode_for_state(has_app_id, phase) == BLE_IBEACON_OFF);
    has_app_id = true;
    assert(ibeacon_mode_for_state(has_app_id, phase) == BLE_IBEACON_EMERGENCY);
    assert(ibeacon_mode_for_state(has_app_id, EMERGENCY_NONE) == BLE_IBEACON_CONNECTION);
    assert(ibeacon_mode_for_state(has_app_id, EMERGENCY_ACKED) == BLE_IBEACON_CONNECTION);
    has_app_id = false;
    assert(ibeacon_mode_for_state(has_app_id, phase) == BLE_IBEACON_OFF);
    return 0;
}
