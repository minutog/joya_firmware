#include "pending_routine.h"
#include <assert.h>
#include <stdint.h>

int main(void)
{
    uint8_t pending = 0;

    pending = pending_routine_next(pending, 2, false, false);
    assert(pending == 0); /* No APP_ID. */

    pending = pending_routine_next(pending, 2, true, false);
    assert(pending == 2);
    assert(pending_routine_next(pending, 0, true, false) == 2);
    pending = pending_routine_next(pending, 3, true, false);
    assert(pending == 3); /* OFF replaces ON; no queue or pair cancellation. */
    pending = pending_routine_next(pending, 2, true, false);
    assert(pending == 2); /* ON replaces OFF. */
    assert(pending_routine_next(pending, 3, true, true) == 0);
    assert(pending_routine_next(0, 2, true, true) == 0);
    assert(pending_routine_next(pending, 3, false, false) == 0);
    return 0;
}
