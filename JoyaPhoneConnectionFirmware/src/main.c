#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "button.h"
#include "app_state.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("Iniciando firmware...");

    // 1. Inicializamos el módulo del botón
    int ret = button_init();
    if (ret != 0) {
        LOG_ERR("Error al inicializar el boton: %d", ret);
        return -1;
    }

    ret = ble_driver_init();
    if (ret) {
        LOG_ERR("Fallo al iniciar BLE. Error: %d", ret);
        return -1;
    }

    LOG_INF("Boton inicializado correctamente. Esperando eventos...");

    fsm_thread_loop();

    // 2. Loop principal
    //while (1) {
        // En un diseño basado en eventos, el main suele quedar libre
        // o manejando tareas de bajo consumo (idle).
        k_sleep(K_FOREVER);
    //}
}