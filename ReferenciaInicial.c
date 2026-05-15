/*
 * Joya - referencia inicial de estructura de firmware.
 *
 * Hardware confirmado:
 * - Nordic nRF52832-QFAA-T
 * - Bateria CR1620
 * - Haptic actuator 10 mm x 2 mm
 * - 1 boton fisico
 *
 * Importante:
 * Este archivo NO es todavia el firmware final Zephyr/NCS.
 * Es un mapa legible de estados, eventos y acciones esperadas.
 */

#include <stdbool.h>

/* Eventos posibles del boton.
 *
 * La funcion read_button_event() deberia devolver un evento una sola vez.
 * Ejemplo: si detecta un single click, devuelve BUTTON_SINGLE_CLICK una vez
 * y despues vuelve a BUTTON_NONE. Asi evitamos repetir la accion infinitamente.
 */
typedef enum {
	BUTTON_NONE = 0,
	BUTTON_SINGLE_CLICK,
	BUTTON_DOUBLE_CLICK,
	BUTTON_TRIPLE_CLICK,
	BUTTON_LONG_SHORT_PRESS,
	BUTTON_LONG_LONG_PRESS,
} button_event_t;

/* Estados principales del producto. */
typedef enum {
	STATE_UNPAIRED = 0,       /* Sin credenciales / sin telefono asociado. */
	STATE_PAIRING_WINDOW,     /* BLE visible por una ventana corta para asociar. */
	STATE_BONDED,             /* Ya asociado; BLE normalmente apagado/dormido. */
	STATE_REPAIRING,          /* Ventana de confirmacion para borrar/re-asociar. */
} app_state_t;

/* Patrones hapticos de feedback para el usuario. */
typedef enum {
	HAPTIC_PAIRING_START = 0,
	HAPTIC_PAIRED_SUCCESS,
	HAPTIC_COMMUTE,
	HAPTIC_EMERGENCY,
	HAPTIC_RESET,
} haptic_pattern_t;

static app_state_t state = STATE_UNPAIRED;

/* Prototipos: en el firmware final estas funciones usarian Zephyr/NCS. */
static void initialize_system(void);
static button_event_t read_button_event(void);
static bool run_pairing_window(void);
static bool repairing_window_expired(void);
static void clear_bonding_data(void);
static void haptic_state(haptic_pattern_t pattern);
static void ble_sleep(void);
static void ble_start_pairing(void);
static void ble_stop(void);
static void ble_send_commute_start(void);
static void ble_send_commute_end(void);
static void ble_start_emergency(void);

int main(void)
{
	initialize_system();
	ble_sleep();

	while (1) {
		/* El boton se lee dentro del loop, antes de evaluar el estado.
		 * En el firmware final, esto probablemente venga de una interrupcion
		 * GPIO + debounce, no de polling continuo.
		 */
		button_event_t button = read_button_event();

		switch (state) {
		case STATE_UNPAIRED:
			/* Estado inicial: el dispositivo no anuncia BLE todo el tiempo.
			 * Solo abre pairing cuando el usuario hace double click.
			 */
			if (button == BUTTON_DOUBLE_CLICK) {
				state = STATE_PAIRING_WINDOW;
				haptic_state(HAPTIC_PAIRING_START);
				ble_start_pairing();
			}
			break;

		case STATE_PAIRING_WINDOW:
			/* Mock simple: run_pairing_window() representa una ventana de
			 * hasta 90 segundos. Devuelve true si el telefono carga bien las
			 * credenciales, false si expira o falla.
			 */
			if (run_pairing_window()) {
				state = STATE_BONDED;
				haptic_state(HAPTIC_PAIRED_SUCCESS);
			} else {
				state = STATE_UNPAIRED;
			}

			ble_stop();
			break;

		case STATE_BONDED:
			/* Como button es un evento local, se consume en esta vuelta.
			 * Eso evita mandar varias veces el mismo comando si no hubo
			 * un nuevo click real.
			 */
			if (button == BUTTON_SINGLE_CLICK) {
				ble_send_commute_start();
				haptic_state(HAPTIC_COMMUTE);
			} else if (button == BUTTON_TRIPLE_CLICK) {
				ble_send_commute_end();
				haptic_state(HAPTIC_COMMUTE);
			} else if (button == BUTTON_LONG_SHORT_PRESS) {
				ble_start_emergency();
				haptic_state(HAPTIC_EMERGENCY);
			} else if (button == BUTTON_LONG_LONG_PRESS) {
				state = STATE_REPAIRING;
				haptic_state(HAPTIC_RESET);
			}
			break;

		case STATE_REPAIRING:
			/* Entra aca con long-long press.
			 * Mock de seguridad: otro long-long press confirma reset.
			 * Si la ventana expira, vuelve a bonded sin borrar nada.
			 */
			if (button == BUTTON_LONG_LONG_PRESS) {
				clear_bonding_data();
				state = STATE_UNPAIRED;
				ble_stop();
			} else if (repairing_window_expired()) {
				state = STATE_BONDED;
			}
			break;
		}

		/* TODO bateria:
		 * En el firmware final no deberia ser un while caliente.
		 * Lo reemplazaremos por espera de eventos/timers de Zephyr.
		 */
	}

	return 0;
}

static void initialize_system(void)
{
	/* Inicializar GPIO, haptic, BLE stack y memoria persistente. */
}

static button_event_t read_button_event(void)
{
	/* Placeholder:
	 * Aca va la logica real de antirebote y deteccion de clicks.
	 * Por ahora devuelve BUTTON_NONE para que este archivo sea estructura.
	 */
	return BUTTON_NONE;
}

static bool run_pairing_window(void)
{
	/* Placeholder:
	 * En firmware final:
	 * - iniciar advertising si no esta activo,
	 * - esperar conexion del celular,
	 * - recibir credenciales,
	 * - cortar por timeout a los 90 segundos.
	 */
	return false;
}

static bool repairing_window_expired(void)
{
	/* Placeholder: luego se reemplaza por un timer real. */
	return false;
}

static void clear_bonding_data(void)
{
	/* Borrar credenciales locales cuando el usuario confirma re-pairing. */
}

static void haptic_state(haptic_pattern_t pattern)
{
	/* Ejecutar el patron haptico indicado con el driver real. */
	(void)pattern;
}

static void ble_sleep(void)
{
	/* Dejar BLE en el estado de menor consumo posible. */
}

static void ble_start_pairing(void)
{
	/* Encender advertising conectable para pairing inicial. */
}

static void ble_stop(void)
{
	/* Apagar advertising / desconectar si corresponde / volver a bajo consumo. */
}

static void ble_send_commute_start(void)
{
	/* Abrir BLE, mandar evento commute_start al telefono y cerrar al confirmar. */
}

static void ble_send_commute_end(void)
{
	/* Abrir BLE, mandar evento commute_end al telefono y cerrar al confirmar. */
}

static void ble_start_emergency(void)
{
	/* Abrir BLE y mantenerlo activo hasta que el telefono confirme fin de emergencia. */
}
