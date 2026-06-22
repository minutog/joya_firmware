#ifndef FLASH_MEMORY_H
#define FLASH_MEMORY_H

#include <zephyr/settings/settings.h>
#include <string.h>
#include <zephyr/logging/log.h>
void storage_init(void);
void storage_save_app_id(const char* new_app_id);
void storage_factory_reset(void);
void storage_save_emergency_state(bool is_active);
bool is_in_emergency(void);

#endif