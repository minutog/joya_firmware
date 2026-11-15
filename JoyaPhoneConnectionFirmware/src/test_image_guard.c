#include "test_image_guard.h"

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>

static atomic_t test_image_active;

static void confirmation_deadline_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (atomic_get(&test_image_active) && !boot_is_img_confirmed()) {
        sys_reboot(SYS_REBOOT_COLD);
    }
    atomic_clear(&test_image_active);
}

K_WORK_DELAYABLE_DEFINE(confirmation_deadline_work, confirmation_deadline_handler);

static enum mgmt_cb_return image_confirmed_handler(uint32_t event, enum mgmt_cb_return prev_status, int32_t *rc, uint16_t *group, bool *abort_more, void *data, size_t data_size) {
    ARG_UNUSED(event);
    ARG_UNUSED(rc);
    ARG_UNUSED(group);
    ARG_UNUSED(abort_more);
    ARG_UNUSED(data);
    ARG_UNUSED(data_size);

    if (atomic_get(&test_image_active) && boot_is_img_confirmed()) {
        atomic_clear(&test_image_active);
        (void)k_work_cancel_delayable(&confirmation_deadline_work);
    }
    return prev_status;
}

static struct mgmt_callback image_confirmed_callback = {
    .callback = image_confirmed_handler,
    .event_id = MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED,
};

void test_image_guard_init(void) {
    mgmt_callback_register(&image_confirmed_callback);
}

void test_image_guard_start(void) {
    if (boot_is_img_confirmed()) {
        return;
    }

    atomic_set(&test_image_active, 1);

    int ret = k_work_reschedule(
        &confirmation_deadline_work,
        K_MINUTES(5)
    );

    if (ret < 0) {
        sys_reboot(SYS_REBOOT_COLD);
    }
}
