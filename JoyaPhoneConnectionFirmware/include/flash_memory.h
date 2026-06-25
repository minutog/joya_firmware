#ifndef FLASH_MEMORY_H
#define FLASH_MEMORY_H

#include <zephyr/settings/settings.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "app_comm.h"

/**
 * @brief Initialize storage and load saved settings.
 * @return 0 on success, or a non-zero value on failure.
 */
int storage_init(void);

/**
 * @brief Save the application identifier to storage.
 * @param new_app_id Pointer to the application identifier buffer.
 * @return 0 on success, or a non-zero value on failure.
 */
int storage_save_app_id(const uint8_t* new_app_id);

/**
 * @brief Clear stored application data and reset cached values.
 * @return 0 on success, or a non-zero value on failure.
 */
int storage_factory_reset(void);

/**
 * @brief Save the emergency state to storage.
 * @param is_active true if emergency mode is active, false otherwise.
 * @return 0 on success, or a non-zero value on failure.
 */
int storage_save_emergency_state(bool is_active);

/**
 * @brief Check whether emergency mode is active.
 * @return true if emergency mode is active, false otherwise.
 */
bool is_in_emergency(void);

/**
 * @brief Check whether the application identifier is empty.
 * @return true if the application identifier is empty, false otherwise.
 */
bool is_app_id_empty(void);

/**
 * @brief Get the stored application identifier.
 * @return Pointer to the application identifier buffer.
 */
const uint8_t* storage_get_app_id(void);

#endif