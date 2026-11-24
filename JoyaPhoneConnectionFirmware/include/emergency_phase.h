#ifndef EMERGENCY_PHASE_H
#define EMERGENCY_PHASE_H

#include <stdbool.h>
#include <stdint.h>

/* Values 0 and 1 match the bool stored by older firmware. */
typedef enum {
    EMERGENCY_NONE = 0,
    EMERGENCY_PENDING_ACK = 1,
    EMERGENCY_ACKED = 2,
} emergency_phase_t;

typedef enum {
    EMERGENCY_TRIGGER,
    EMERGENCY_ACK,
    EMERGENCY_STOP,
} emergency_action_t;

emergency_phase_t emergency_phase_transition(emergency_phase_t phase,
                                             emergency_action_t action);
bool emergency_phase_decode(uint8_t stored, emergency_phase_t *phase);
bool emergency_phase_needs_notification(emergency_phase_t phase);

#endif
