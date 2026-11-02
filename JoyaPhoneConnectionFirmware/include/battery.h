#ifndef JOYA_BATTERY_H_
#define JOYA_BATTERY_H_

#include <stdint.h>

#define BATTERY_MONITOR_INTERVAL_MS 60000
#define BATTERY_VERIFY_INTERVAL_MS  10000
#define BATTERY_VERIFY_SAMPLES      6
#define BATTERY_VERIFY_MIN_VOTES    5

#define BATTERY_LEVEL_1_MV          3500
#define BATTERY_LEVEL_2_MV          3400

/* Para recuperarse, cada umbral exige superar su valor por este margen. */
#define BATTERY_HYSTERESIS_MV       50

/* Inicializar el canal SAADC antes de leer o iniciar el monitoreo. */
int battery_init(void);

/* Leer VBAT en milivoltios. Esta llamada espera a que termine una conversión. */
int battery_get_voltage_mv(int32_t *voltage_mv);

/* Programar la primera medición de inmediato y luego cada intervalo. */
int battery_monitor_start(void);
int battery_monitor_stop(void);

/* Devuelve -ENODATA hasta la primera medición periódica exitosa. */
int battery_get_last_voltage_mv(int32_t *voltage_mv);

#endif /* JOYA_BATTERY_H_ */
