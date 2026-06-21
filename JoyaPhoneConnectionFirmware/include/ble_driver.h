#ifndef BLE_DRIVER_H
#define BLE_DRIVER_H

#include <stdint.h>

void ble_force_reset(void);


/**
 * @brief Inicializa el stack de Bluetooth y registra los callbacks.
 */
int ble_driver_init(void);

int ble_send_event_secure(uint8_t event_byte);

/**
 * @brief Inicia el anuncio (Advertising) visible como "Joya Setup".
 * Usado cuando el dispositivo no está reclamado.
 */
int ble_start_setup_advertising(void);

/**
 * @brief Inicia el anuncio (Advertising) visible como "Joya".
 * Usado para reconexión cuando ya está emparejado.
 */
int ble_start_reconnect_advertising(void);

/**
 * @brief Detiene cualquier anuncio en curso.
 */
int ble_stop_advertising(void);

/**
 * @brief Envía una notificación a la App celular (Ej: 0x01 pulso, 0x02 emergencia).
 * @param event_byte El código de 1 byte a enviar.
 */
int ble_send_notify(uint8_t event_byte);

#endif // BLE_DRIVER_H