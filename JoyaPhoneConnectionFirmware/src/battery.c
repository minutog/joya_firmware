#include "battery.h"
#include "app_state.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define BATTERY_ADC_NODE DT_PATH(zephyr_user)
#define BATTERY_R_TOP_KOHM 120
#define BATTERY_R_BOTTOM_KOHM 82

static const struct adc_dt_spec battery_adc =
	ADC_DT_SPEC_GET_BY_NAME(BATTERY_ADC_NODE, battery);

static atomic_t initialized;
static atomic_t last_valid;
static atomic_t last_voltage_mv;

enum battery_level {
	BATTERY_STATE_UNKNOWN = -1,
	BATTERY_STATE_NORMAL = 0,
	BATTERY_STATE_LEVEL_1,
	BATTERY_STATE_LEVEL_2,
};

K_MUTEX_DEFINE(battery_monitor_lock);
static bool monitoring;
static bool verifying;
static uint32_t monitor_generation;
static uint8_t verification_count;
static uint8_t votes[3];
static enum battery_level confirmed_level = BATTERY_STATE_UNKNOWN;

static void battery_monitor_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_monitor_work, battery_monitor_work_handler);

int battery_init(void)
{
	int err;

	if (atomic_get(&initialized)) {
		return 0;
	}

	if (!device_is_ready(battery_adc.dev)) {
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&battery_adc);
	if (err < 0) {
		return err;
	}

	atomic_set(&initialized, 1);
	return 0;
}

int battery_get_voltage_mv(int32_t *voltage_mv)
{
	int16_t sample;
	int32_t adc_mv;
	struct adc_sequence sequence = {
		.buffer = &sample,
		.buffer_size = sizeof(sample),	// sample es de 16 bits -> sizeof(sample) = 2
	};
	int err;

	if (voltage_mv == NULL) {
		return -EINVAL;
	}
	if (!atomic_get(&initialized)) {
		return -EACCES;
	}

	adc_sequence_init_dt(&battery_adc, &sequence);
	err = adc_read_dt(&battery_adc, &sequence);
	if (err < 0) {
		return err;
	}

	adc_mv = sample;
	err = adc_raw_to_millivolts_dt(&battery_adc, &adc_mv);
	if (err < 0) {
		return err;
	}

	/* Redondear al mV más cercano luego de deshacer el divisor. */
	*voltage_mv = (adc_mv * (BATTERY_R_TOP_KOHM + BATTERY_R_BOTTOM_KOHM) +
		       BATTERY_R_BOTTOM_KOHM / 2) / BATTERY_R_BOTTOM_KOHM;
	return 0;
}

/* La histéresis se aplica solo al cruzar hacia arriba un umbral ya confirmado. */
static enum battery_level battery_classify(int32_t voltage_mv)
{
	// Primero verifico LEVEL 2 porque es el crítico
	// Si ya estabamos en nivel 2, verifico si votalge_mv <= BATTERY_LEVEL_2 + 50 mV
	// Si no estabamos en nivel 2, verifico si voltage_mv <= BATTERY_LEVEL_2
	if (voltage_mv <= BATTERY_LEVEL_2_MV +
		(confirmed_level >= BATTERY_STATE_LEVEL_2 ? BATTERY_HYSTERESIS_MV : 0)) {
		return BATTERY_STATE_LEVEL_2;
	}

	// Si ya estabamos en nivel 1, verifico si voltage_mv <= BATTERY_LEVEL_1 + 50 mV
	// Si no estabamos en nivel 1, verifico si voltage_mv <= BATTERY_LEVEL_1
	if (voltage_mv <= BATTERY_LEVEL_1_MV +
		(confirmed_level >= BATTERY_STATE_LEVEL_1 ? BATTERY_HYSTERESIS_MV : 0)) {
		return BATTERY_STATE_LEVEL_1;
	}

	// Si en no estaba en ninguna alerta y no es menor -> BATTERY NORMAL
	// Si estaba en algún nivel y supere la histéresis -> BATTERY NORMAL
	return BATTERY_STATE_NORMAL;
}

static uint8_t votes_at_or_below(enum battery_level level)
{
	uint8_t count = 0;

	// A BATTERY_LEVEL_1 le sumo los de BATTERY_LEVEL_1 y los de BATTERY_LEVEL_2 (si level 1 = 3, level 2 = 2 y necesito 5, level 2 no se puede confirmar pero level 1 sí)
	for (int i = level; i <= BATTERY_STATE_LEVEL_2; ++i) {
		count += votes[i];
	}
	return count;
}

static uint8_t votes_at_or_above(enum battery_level level)
{
	uint8_t count = 0;

	// Para recueprar cuento al revés, si estoy en level 2 cuento los de normal y level 1 para ver si sube uno
	for (int i = BATTERY_STATE_NORMAL; i <= level; ++i) {
		count += votes[i];
	}
	return count;
}

static event_type_t battery_event_for_level(enum battery_level level)
{
	switch (level) {
	case BATTERY_STATE_LEVEL_2:
		return EV_BATTERY_LEVEL_2;
	case BATTERY_STATE_LEVEL_1:
		return EV_BATTERY_LEVEL_1;
	default:
		return EV_BATTERY_NORMAL;
	}
}

static void battery_finish_verification(void)
{
	enum battery_level next = BATTERY_STATE_UNKNOWN;

	/* Votos acumulados: una muestra LEVEL 2 también vota por LEVEL 1. */
	// Arranco por el peor caso (LEVEL 2), despues veo los de LEVEL1 + LEVEL 2. En el primero que consiga los botos, break
	for (int level = BATTERY_STATE_LEVEL_2; level > confirmed_level && level >= BATTERY_STATE_LEVEL_1;
	     --level) {
		if (votes_at_or_below(level) >= BATTERY_VERIFY_MIN_VOTES) {
			next = level;
			break;
		}
	}

	/* Recuperación: una muestra NORMAL también vota por los niveles superiores. */
	// Arranco por el estado normal, sigo con LEVEL1 (NORMAL + LEVEL1, es superiores a) y luego con LEVEL2 -> el primero: break
	if (next == BATTERY_STATE_UNKNOWN) {
		for (int level = BATTERY_STATE_NORMAL; level < confirmed_level; ++level) {
			if (votes_at_or_above(level) >= BATTERY_VERIFY_MIN_VOTES) {
				next = level;
				break;
			}
		}
	}

	// Caso JOYA acaba de arrancar
	if (next == BATTERY_STATE_UNKNOWN && confirmed_level == BATTERY_STATE_UNKNOWN &&
	    votes[BATTERY_STATE_NORMAL] >= BATTERY_VERIFY_MIN_VOTES) {
		next = BATTERY_STATE_NORMAL;
	}

	// Los votos no alcanzan para ninguna: sigue en el mismo nivel
	if (next == BATTERY_STATE_UNKNOWN || next == confirmed_level) {
		return;
	}

	// Si es la primera vez y pasamos a normal -> dejamos el nivel confirmado
	// Si no es la primera vez -> se añade el evento y dejamos confirmado
	if ((confirmed_level == BATTERY_STATE_UNKNOWN && next == BATTERY_STATE_NORMAL) ||
	    add_event(battery_event_for_level(next)) == 0) {
		confirmed_level = next;
	}
}

static void battery_monitor_work_handler(struct k_work *work)
{
	int32_t voltage_mv;
	uint32_t generation;
	bool was_verifying;
	int err;

	ARG_UNUSED(work);

	k_mutex_lock(&battery_monitor_lock, K_FOREVER);
	// 1. Comprobación de monitoreo activo
	if (!monitoring) {
		k_mutex_unlock(&battery_monitor_lock);
		return;
	}

	// 2. Copio los valores de las variables globales a las locales (porque después vamos a liberar el mutex)
	generation = monitor_generation;
	was_verifying = verifying;
	k_mutex_unlock(&battery_monitor_lock);

	// 3. Medición
	err = battery_get_voltage_mv(&voltage_mv);

	// 4. Se comprueba si la medición es válida (¿alguien detuvo el monitoreo mientras se estaba midiendo? || ¿alguien detuvo el monitoreo y la volvió a iniciar mientras estaba midiendo?)
	k_mutex_lock(&battery_monitor_lock, K_FOREVER);
	if (!monitoring || generation != monitor_generation) {
		k_mutex_unlock(&battery_monitor_lock);
		return;
	}

	// 5. Guardo los valores de la medición
	if (err == 0) {
		atomic_set(&last_voltage_mv, voltage_mv);
		atomic_set(&last_valid, 1);
	}

	// 6. Si was_verifying == true -> esta medición forma parte de las 6 muestras de verificación
	if (was_verifying) {
		if (err == 0) {
			votes[battery_classify(voltage_mv)]++;
		}
		verification_count++;

		// 6.1. Ya llegamos a las muestras de verificación
		if (verification_count == BATTERY_VERIFY_SAMPLES) {
			// Evalua la votación, finaliza la verificación y programa para el monitoreo normal
			battery_finish_verification();
			verifying = false;
			(void)k_work_reschedule(&battery_monitor_work,
						K_MSEC(BATTERY_MONITOR_INTERVAL_MS));
		} else {
			// Vuelve a programar para el monitoreo de verificación
			(void)k_work_reschedule(&battery_monitor_work,
						K_MSEC(BATTERY_VERIFY_INTERVAL_MS));
		}
	} 
	// 7. No estábamos verificando pero se detecta un cambio de nivel
	else if (err == 0 && battery_classify(voltage_mv) != confirmed_level) {
		// Se habilita la erificación, se resetea el contador y los votos y se programa para dentro de un tiempo de verificación
		verifying = true;
		verification_count = 0;
		memset(votes, 0, sizeof(votes));
		(void)k_work_reschedule(&battery_monitor_work,
					K_MSEC(BATTERY_VERIFY_INTERVAL_MS));
	} 
	// 8. No estábamos verificando y no hay cambio de nivel -> se reprograma para el tiempo normal 
	else {

		(void)k_work_reschedule(&battery_monitor_work,
					K_MSEC(BATTERY_MONITOR_INTERVAL_MS));
	}
	k_mutex_unlock(&battery_monitor_lock);
}

int battery_monitor_start(void)
{
	if (!atomic_get(&initialized)) {
		return -EACCES;
	}
	k_mutex_lock(&battery_monitor_lock, K_FOREVER);

	// Si no se estaba monitoreando, iniciamos el monitoreo
	if (!monitoring) {
		monitoring = true;
		// Se incrementa el ID de este monitoreo
		monitor_generation++;
		verifying = false;
		verification_count = 0;	
		// Arranca el monitoreo
		(void)k_work_reschedule(&battery_monitor_work, K_NO_WAIT);
	}
	k_mutex_unlock(&battery_monitor_lock);
	return 0;
}


int battery_monitor_stop(void)
{
	k_mutex_lock(&battery_monitor_lock, K_FOREVER);
	// Frena el monitoreo, cancela la tarea y cambia el ID del monitoreo
	monitoring = false;
	monitor_generation++;
	verifying = false;
	verification_count = 0;
	(void)k_work_cancel_delayable(&battery_monitor_work);
	k_mutex_unlock(&battery_monitor_lock);
	return 0;
}

// Devuelve por argumento la última medición
int battery_get_last_voltage_mv(int32_t *voltage_mv)
{
	if (voltage_mv == NULL) {
		return -EINVAL;
	}
	if (!atomic_get(&last_valid)) {
		return -ENODATA;
	}

	*voltage_mv = atomic_get(&last_voltage_mv);
	return 0;
}
