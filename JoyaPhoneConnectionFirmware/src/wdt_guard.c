#include "wdt_guard.h"
#include "app_state.h"

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <hal/nrf_wdt.h>

#define WDT_TIMEOUT_MS 30000U
#define GUARD_INTERVAL_MS 10000U
#define FSM_STALL_MS 15000U
#define WDT_CRV_TICKS ((uint32_t)(((uint64_t)WDT_TIMEOUT_MS * 32768ULL) / 1000ULL))

static const struct device *const wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));
static int wdt_channel;
static bool watchdog_ready;
static atomic_t main_ready;
static atomic_t fsm_processing;
static atomic_t fsm_started_ms;
static atomic_t fsm_progress;
static uint32_t pending_since_ms;
static atomic_val_t observed_progress;

static bool wdt_registers_match(void)
{
    return NRF_WDT->CRV == WDT_CRV_TICKS &&
           NRF_WDT->RREN == 1U &&
           NRF_WDT->CONFIG == WDT_CONFIG_SLEEP_Msk;
}

static bool wdt_init_failed(void)
{
    /* A running WDT cannot be reconfigured. Let it reset the chip, clearing
     * stale registers left by a different application after a software reset.
     */
    if (nrf_wdt_started_check(NRF_WDT)) {
        /* Stay awake even if an inherited CONFIG pauses WDT during sleep. */
        for (;;) {
            __NOP();
        }
    }
    /* No hardware reset is available. Continue boot without WDT so BLE OTA
     * remains possible; do not enter a fast software-reset loop.
     */
    return false;
}

static void guard_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(guard_work, guard_work_handler);

static void guard_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    uint32_t now = k_uptime_get_32();

    if (atomic_get(&main_ready)) {
        bool healthy = true;
        atomic_val_t progress = atomic_get(&fsm_progress);

        if (atomic_get(&fsm_processing) &&
            (uint32_t)(now - (uint32_t)atomic_get(&fsm_started_ms)) > FSM_STALL_MS) {
            healthy = false;
        }

        if (app_state_pending_events() == 0U) {
            pending_since_ms = 0U;
        } else if (pending_since_ms == 0U || progress != observed_progress) {
            pending_since_ms = now;
        } else if ((uint32_t)(now - pending_since_ms) > FSM_STALL_MS) {
            healthy = false;
        }
        observed_progress = progress;

        if (healthy && wdt_feed(wdt, wdt_channel) != 0) {
            /* No further feeds if the driver reports an error. */
            return;
        }
    }

    (void)k_work_reschedule(&guard_work, K_MSEC(GUARD_INTERVAL_MS));
}

bool wdt_guard_init(void)
{
    const struct wdt_timeout_cfg cfg = {
        .window = {.min = 0U, .max = WDT_TIMEOUT_MS},
        .callback = NULL,
        .flags = WDT_FLAG_RESET_SOC,
    };

    if (!device_is_ready(wdt) ||
        (nrf_wdt_started_check(NRF_WDT) && !wdt_registers_match())) {
        return wdt_init_failed();
    }

    wdt_channel = wdt_install_timeout(wdt, &cfg);
    if (wdt_channel != 0 ||
        wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG) != 0 ||
        !nrf_wdt_started_check(NRF_WDT) || !wdt_registers_match()) {
        return wdt_init_failed();
    }

    watchdog_ready = true;
    (void)k_work_reschedule(&guard_work, K_MSEC(GUARD_INTERVAL_MS));
    return true;
}

void wdt_guard_main_ready(void)
{
    if (!watchdog_ready) {
        return;
    }
    atomic_set(&main_ready, 1);
    (void)k_work_reschedule(&guard_work, K_NO_WAIT);
}

void wdt_guard_fsm_event_begin(void)
{
    atomic_set(&fsm_started_ms, (atomic_val_t)k_uptime_get_32());
    atomic_set(&fsm_processing, 1);
}

void wdt_guard_fsm_event_end(void)
{
    atomic_inc(&fsm_progress);
    atomic_clear(&fsm_processing);
}
