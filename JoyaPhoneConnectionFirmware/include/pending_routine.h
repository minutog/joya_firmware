#ifndef PENDING_ROUTINE_H
#define PENDING_ROUTINE_H

#include <stdbool.h>
#include <stdint.h>

/* Only the latest routine matters. An emergency discards all routines. */
static inline uint8_t pending_routine_next(uint8_t current, uint8_t generated,
                                            bool has_app_id, bool emergency_active)
{
    if (!has_app_id || emergency_active) {
        return 0;
    }
    return generated != 0 ? generated : current;
}

#endif
