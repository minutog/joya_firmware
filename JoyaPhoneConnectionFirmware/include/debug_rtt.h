#pragma once

#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_USE_SEGGER_RTT)
#include <SEGGER_RTT.h>

static inline void dbg_rtt_mark(const char *msg)
{
    SEGGER_RTT_WriteString(0, msg);
}

#else

static inline void dbg_rtt_mark(const char *msg)
{
    ARG_UNUSED(msg);
}

#endif