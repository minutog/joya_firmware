#ifndef FLASH_MEMORY_H
#define FLASH_MEMORY_H

#include <zephyr/settings/settings.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "app_comm.h"

int storage_init(void);
int storage_save_app_id(const uint8_t* new_app_id);
void storage_factory_reset(void);
void storage_save_emergency_state(bool is_active);
bool is_in_emergency(void);
bool is_app_id_empty(void);
const uint8_t* storage_get_app_id(void);

#endif