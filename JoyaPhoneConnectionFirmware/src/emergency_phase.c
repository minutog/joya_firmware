#include "emergency_phase.h"
#include <stddef.h>

emergency_phase_t emergency_phase_transition(emergency_phase_t phase,
                                             emergency_action_t action)
{
    switch (action) {
    case EMERGENCY_TRIGGER:
        return EMERGENCY_PENDING_ACK;
    case EMERGENCY_ACK:
        return phase == EMERGENCY_NONE ? EMERGENCY_NONE : EMERGENCY_ACKED;
    case EMERGENCY_STOP:
        return EMERGENCY_NONE;
    default:
        return phase;
    }
}

bool emergency_phase_needs_notification(emergency_phase_t phase)
{
    return phase == EMERGENCY_PENDING_ACK;
}

bool emergency_phase_decode(uint8_t stored, emergency_phase_t *phase)
{
    if (phase == NULL || stored > EMERGENCY_ACKED) {
        return false;
    }
    *phase = (emergency_phase_t)stored;
    return true;
}
