#ifndef WDT_GUARD_H
#define WDT_GUARD_H

#include <stdbool.h>

/* Called once at startup, before other application initialization. */
bool wdt_guard_init(void);
void wdt_guard_main_ready(void);

/* Bracket only actual FSM event handling; idle K_FOREVER waits are healthy. */
void wdt_guard_fsm_event_begin(void);
void wdt_guard_fsm_event_end(void);

#endif
