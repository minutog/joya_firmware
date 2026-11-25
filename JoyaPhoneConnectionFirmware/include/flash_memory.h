#ifndef FLASH_MEMORY_H
#define FLASH_MEMORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "app_comm.h"
#include "emergency_phase.h"

#include <zephyr/settings/settings.h>

#define JOYA_ADV_NAME_DEFAULT "Joya"
#define JOYA_NAME_PREFIX      "Joya de "
#define JOYA_NAME_MAX_LEN     31


const char *storage_get_name(void);
int storage_save_name(const char *name);

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

/** Update the emergency phase in RAM and persist it when flash is available. */
int storage_save_emergency_phase(emergency_phase_t phase);
emergency_phase_t storage_get_emergency_phase(void);
uint8_t storage_get_pending_routine(void);
/* RAM changes first. Return 1 when the flash update fails. */
int storage_save_pending_routine(uint8_t command);

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
