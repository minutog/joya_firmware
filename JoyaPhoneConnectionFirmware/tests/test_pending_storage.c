#define _GNU_SOURCE
#define FLASH_MEMORY_H
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "emergency_phase.h"

#define SIZE_APP_ID 16
#define JOYA_NAME_MAX_LEN 31
#define JOYA_ADV_NAME_DEFAULT "Joya"
#define COMMAND_ROUTINE 2
#define COMMAND_END_ROUTINE 3
#define SETTINGS_STATIC_HANDLER_DEFINE(...)

typedef int (*settings_read_cb)(void *, void *, size_t);
static int settings_subsys_init(void);
static int settings_load(void);
static int settings_save_one(const char *key, const void *data, size_t len);
static int settings_delete(const char *key);
static bool settings_name_steq(const char *name, const char *key,
                                const char **next);
bool is_app_id_empty(void);

#include "../src/flash_memory.c"

typedef struct {
    const char *key;
    uint8_t data[32];
    size_t len;
    bool valid;
} fake_setting_t;

static fake_setting_t settings[] = {
    {.key = "joya/app_id"},
    {.key = "joya/emergency"},
    {.key = "joya/routine"},
    {.key = "joya/name"},
};
static int writes;
static bool fail_routine_write;
static bool fail_routine_delete;

void diagnostics_report_error(diagnostics_error_t error) { (void)error; }

static fake_setting_t *find_setting(const char *key)
{
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
        if (strcmp(settings[i].key, key) == 0) {
            return &settings[i];
        }
    }
    return NULL;
}

static int settings_subsys_init(void) { return 0; }

static int fake_read(void *arg, void *buf, size_t len)
{
    fake_setting_t *s = arg;
    memcpy(buf, s->data, len);
    return (int)len;
}

static bool settings_name_steq(const char *name, const char *key,
                                const char **next)
{
    if (strcmp(name, key) != 0) {
        return false;
    }
    *next = NULL;
    return true;
}

static int settings_load(void)
{
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
        if (settings[i].valid) {
            int rc = app_settings_set(settings[i].key + 5, settings[i].len,
                                      fake_read, &settings[i]);
            if (rc != 0) {
                return rc;
            }
        }
    }
    return 0;
}

static int settings_save_one(const char *key, const void *data, size_t len)
{
    fake_setting_t *s = find_setting(key);
    assert(s && len <= sizeof(s->data));
    if (fail_routine_write && strcmp(key, "joya/routine") == 0) {
        return -EIO;
    }
    if (s->valid && s->len == len && memcmp(s->data, data, len) == 0) {
        return 0;
    }
    memcpy(s->data, data, len);
    s->len = len;
    s->valid = true;
    writes++;
    return 0;
}

static int settings_delete(const char *key)
{
    fake_setting_t *s = find_setting(key);
    assert(s);
    if (fail_routine_delete && strcmp(key, "joya/routine") == 0) {
        return -EIO;
    }
    if (s->valid) {
        s->valid = false;
        writes++;
    }
    return 0;
}

int main(void)
{
    const uint8_t app_id[SIZE_APP_ID] = {1};
    assert(storage_init() == 0);
    assert(storage_save_app_id(app_id) == 0);
    assert(storage_save_pending_routine(COMMAND_ROUTINE) == 0);
    int after_on = writes;
    assert(storage_save_pending_routine(COMMAND_ROUTINE) == 0);
    assert(writes == after_on);
    assert(storage_save_pending_routine(COMMAND_END_ROUTINE) == 0);
    assert(storage_init() == 0); /* Reboot restores only the latest routine. */
    assert(storage_get_pending_routine() == COMMAND_END_ROUTINE);
    assert(storage_save_pending_routine(0) == 0);
    assert(storage_init() == 0);
    assert(storage_get_pending_routine() == 0);

    assert(storage_save_pending_routine(COMMAND_ROUTINE) == 0);
    assert(storage_save_emergency_phase(EMERGENCY_PENDING_ACK) == 0);
    assert(storage_init() == 0); /* Cut before the routine tombstone. */
    assert(storage_get_pending_routine() == 0);
    assert(storage_get_emergency_phase() == EMERGENCY_PENDING_ACK);

    assert(storage_save_pending_routine(COMMAND_ROUTINE) == -EBUSY);
    /* Model a stale value left by a cut or an older image. */
    fake_setting_t *routine = find_setting("joya/routine");
    routine->data[0] = COMMAND_ROUTINE;
    memcpy(routine->data + 1, app_id, SIZE_APP_ID);
    routine->len = 1 + SIZE_APP_ID;
    routine->valid = true;
    assert(storage_init() == 0);
    fail_routine_write = true;
    /* Recreate a failure while clearing a stale persisted command. */
    routine->data[0] = COMMAND_ROUTINE;
    memcpy(routine->data + 1, app_id, SIZE_APP_ID);
    routine->len = 1 + SIZE_APP_ID;
    assert(storage_init() == 0);
    assert(storage_save_pending_routine(0) == 1);
    assert(storage_save_emergency_phase(EMERGENCY_NONE) == 1);
    assert(storage_init() == 0); /* Failed clear cannot expose the old routine. */
    assert(storage_get_pending_routine() == 0);
    assert(storage_get_emergency_phase() == EMERGENCY_PENDING_ACK);
    fail_routine_write = false;

    fail_routine_delete = true;
    assert(storage_factory_reset() == 1);
    fail_routine_write = true;
    assert(storage_init() == 0);
    assert(is_app_id_empty());
    assert(storage_save_app_id(app_id) == 1);
    assert(is_app_id_empty()); /* New owner cannot inherit stale state. */
    fail_routine_write = false;

    /* An older image may register another APP_ID without deleting this key. */
    const uint8_t new_app_id[SIZE_APP_ID] = {2};
    assert(settings_save_one("joya/app_id", new_app_id, SIZE_APP_ID) == 0);
    assert(storage_init() == 0);
    assert(storage_get_pending_routine() == 0);
    assert(memcmp(storage_get_app_id(), new_app_id, SIZE_APP_ID) == 0);
    return 0;
}
