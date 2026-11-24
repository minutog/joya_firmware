#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdint.h>

#define DIAG_INIT_BLE       (1u << 0)
#define DIAG_INIT_BUTTON    (1u << 1)
#define DIAG_INIT_SETTINGS  (1u << 2)
#define DIAG_INIT_HAPTIC    (1u << 3)
#define DIAG_INIT_BATTERY   (1u << 4)

typedef enum {
    DIAG_ERROR_FLASH_WRITE = 0xE1,
    DIAG_ERROR_SETTINGS_LOAD = 0xE2,
    DIAG_ERROR_EVENT_QUEUE_FULL = 0xE3,
    DIAG_ERROR_BLE = 0xE4,
    DIAG_ERROR_I2C = 0xE5,
} diagnostics_error_t;

void diagnostics_record_init(uint8_t bit, int result);
void diagnostics_finish_init(void);
void diagnostics_get_init_message(uint8_t out[2]);
void diagnostics_request_init_notification(void);
void diagnostics_report_error(diagnostics_error_t error);

#endif
