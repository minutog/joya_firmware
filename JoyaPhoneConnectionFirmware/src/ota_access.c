#include "ota_access.h"

#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>

#define OTA_IDLE_TIMEOUT K_MINUTES(2)

static K_MUTEX_DEFINE(ota_mutex);
static struct k_work_delayable idle_close_work;
static bool service_enabled;
static bool uploading;
static bool upload_complete;
static bool close_requested;

static void close_service_locked(void)
{
    if (service_enabled && smp_bt_unregister() == 0) {
        service_enabled = false;
        close_requested = false;
    }
}

static void idle_close_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    k_mutex_lock(&ota_mutex, K_FOREVER);
    if (!uploading && !upload_complete) {
        close_service_locked();
    }
    k_mutex_unlock(&ota_mutex);
}

static enum mgmt_cb_return image_status_handler(uint32_t event,
                                                 enum mgmt_cb_return prev_status,
                                                 int32_t *rc, uint16_t *group,
                                                 bool *abort_more, void *data,
                                                 size_t data_size)
{
    ARG_UNUSED(rc);
    ARG_UNUSED(group);
    ARG_UNUSED(abort_more);
    ARG_UNUSED(data);
    ARG_UNUSED(data_size);

    k_mutex_lock(&ota_mutex, K_FOREVER);
    if (event == MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK ||
        event == MGMT_EVT_OP_IMG_MGMT_DFU_STARTED) {
        uploading = true;
        upload_complete = false;
        (void)k_work_cancel_delayable(&idle_close_work);
    } else if (event == MGMT_EVT_OP_IMG_MGMT_DFU_PENDING) {
        uploading = false;
        upload_complete = true;
    } else if (event == MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED) {
        uploading = false;
        if (!upload_complete) {
            if (close_requested) {
                (void)k_work_reschedule(&idle_close_work, K_SECONDS(1));
            } else if (service_enabled) {
                (void)k_work_reschedule(&idle_close_work, OTA_IDLE_TIMEOUT);
            }
        }
    }
    k_mutex_unlock(&ota_mutex);
    return prev_status;
}

static struct mgmt_callback image_status_callback = {
    .callback = image_status_handler,
    .event_id = MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK |
                MGMT_EVT_OP_IMG_MGMT_DFU_STARTED |
                MGMT_EVT_OP_IMG_MGMT_DFU_PENDING |
                MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED,
};

int ota_access_init(void)
{
    int err;

    k_work_init_delayable(&idle_close_work, idle_close_handler);
    err = smp_bt_unregister();
    if (err) {
        return err;
    }
    mgmt_callback_register(&image_status_callback);
    return 0;
}

int ota_access_enable(void)
{
    int err = 0;

    k_mutex_lock(&ota_mutex, K_FOREVER);
    if (!service_enabled) {
        err = smp_bt_register();
        if (err == 0) {
            service_enabled = true;
        }
    }
    if (err == 0) {
        close_requested = false;
        if (!uploading && !upload_complete) {
            (void)k_work_reschedule(&idle_close_work, OTA_IDLE_TIMEOUT);
        }
    }
    k_mutex_unlock(&ota_mutex);
    return err;
}

void ota_access_disable(void)
{
    k_mutex_lock(&ota_mutex, K_FOREVER);
    if (uploading || upload_complete) {
        close_requested = true;
    } else {
        (void)k_work_cancel_delayable(&idle_close_work);
        close_service_locked();
    }
    k_mutex_unlock(&ota_mutex);
}

void ota_access_on_disconnect(void)
{
    k_mutex_lock(&ota_mutex, K_FOREVER);
    uploading = false;
    upload_complete = false;
    close_requested = false;
    (void)k_work_cancel_delayable(&idle_close_work);
    close_service_locked();
    k_mutex_unlock(&ota_mutex);
}

bool ota_access_is_update_active(void)
{
    bool active;

    k_mutex_lock(&ota_mutex, K_FOREVER);
    active = uploading || upload_complete;
    k_mutex_unlock(&ota_mutex);
    return active;
}
