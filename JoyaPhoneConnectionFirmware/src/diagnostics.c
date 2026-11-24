#include "diagnostics.h"
#include "ble_driver.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

static atomic_t init_bits;
static atomic_t init_complete;
/* One pending message of each kind; overwritten or discarded on contention. */
static struct k_spinlock pending_lock;
static uint32_t pending_init_session;
static uint32_t pending_error_session;
static uint8_t pending_error_code;

static void diagnostics_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    k_spinlock_key_t key = k_spin_lock(&pending_lock);
    uint32_t init_session = pending_init_session;
    uint32_t error_session = pending_error_session;
    uint8_t error_code = pending_error_code;
    pending_init_session = 0;
    pending_error_session = 0;
    k_spin_unlock(&pending_lock, key);

    if (init_session != 0) {
        uint8_t message[2];
        diagnostics_get_init_message(message);
        ble_diagnostics_notify_for_session(init_session, message, sizeof(message));
    }

    if (error_session != 0) {
        ble_diagnostics_notify_for_session(error_session, &error_code,
                                           sizeof(error_code));
    }
}

K_WORK_DEFINE(diagnostics_work, diagnostics_work_handler);

void diagnostics_record_init(uint8_t bit, int result)
{
    if (result == 0) {
        atomic_or(&init_bits, bit);
    }
}

void diagnostics_finish_init(void)
{
    atomic_set(&init_complete, 1);
    diagnostics_request_init_notification();
}

void diagnostics_get_init_message(uint8_t out[2])
{
    out[0] = 0xA0;
    out[1] = (uint8_t)atomic_get(&init_bits);
}

void diagnostics_request_init_notification(void)
{
    if (!atomic_get(&init_complete)) {
        return;
    }
    uint32_t session = ble_diagnostics_session();
    if (session != 0) {
        k_spinlock_key_t key = k_spin_lock(&pending_lock);
        pending_init_session = session;
        k_spin_unlock(&pending_lock, key);
        (void)k_work_submit(&diagnostics_work);
    }
}

void diagnostics_report_error(diagnostics_error_t error)
{
    uint32_t session = ble_diagnostics_session();
    if (session == 0) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&pending_lock);
    pending_error_code = error;
    pending_error_session = session;
    k_spin_unlock(&pending_lock, key);
    (void)k_work_submit(&diagnostics_work);
}
