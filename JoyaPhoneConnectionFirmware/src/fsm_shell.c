#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include "app_state.h"

/**
 * @brief Function to send an event to the FSM through the message queue.
 * @param sh Shell context (provided by Zephyr shell)
 * @param argc Argument count (number of arguments passed to the command)
 * @param argv Argument vector (array of strings representing the arguments)
 * @return 0 on success, or an error code if the event could not be sent
 */
static int cmd_send_event(const struct shell *sh, size_t argc, char **argv) {
    // argv[1] es el primer argumento (el número de evento)
    
    event_type_t ev;
    uint8_t is_val = 0;

    if(strcmp(argv[1], "1") == 0){
        ev = EV_BTN_1_PULSE;
        is_val = 1;
    } else if(strcmp(argv[1], "2") == 0){
        ev = EV_BTN_2_PULSE;
        is_val = 1;
    } else if(strcmp(argv[1], "emergencia") == 0){
        ev = EV_BTN_EMERGENCY;
        is_val = 1;
    } else if(strcmp(argv[1], "stop_emergencia") == 0){
        ev = EV_APP_CMD_STOP_EMERGENCY;
        is_val = 1;
    } else if(strcmp(argv[1], "conectado") == 0){
        ev = EV_BLE_CONNECTED;
        is_val = 1;
    } else if(strcmp(argv[1], "desconectado") == 0){
        ev = EV_BLE_DISCONNECTED;
        is_val = 1;
    } else if(strcmp(argv[1], "timeout") == 0){
        ev = EV_BLE_TIMEOUT;
        is_val = 1;
    } else if(strcmp(argv[1], "factory_reset") == 0){
        ev = EV_BTN_FACTORY_RESET;
        is_val = 1;
    } else if(strcmp(argv[1], "long") == 0){
        ev = EV_BTN_LONG_PRESS;
        is_val = 1;
    } else if(strcmp(argv[1], "follow_me") == 0){
        ev = EV_APP_CMD_FOLLOW_ME;
        is_val = 1;
    } else if(strcmp(argv[1], "ack_emergency") == 0){
        ev = EV_APP_ACK_EMERGENCY;
        is_val = 1;
    } else {
        shell_print(sh, "Evento no reconocido. Usa: 1, 2, emergencia, stop_emergencia, conectado, desconectado, timeout, factory_reset, long, follow_me, ack_emergency");
        return 0;
    }

    if(!is_val){
        shell_print(sh, "Evento no reconocido. Usa: 1, 2, emergencia, stop_emergencia, conectado, desconectado, timeout, factory_reset, long, follow_me, ack_emergency");
        return 0;
    }

    // Inyectamos en la cola
    int ret = add_event(ev);
    
    if (ret == 0) {
        shell_print(sh, "Evento %c enviado correctamente a la FSM", ev);
    } else {
        shell_print(sh, "Error: Cola llena o evento no enviado");
    }

    return 0;
}

// Registrar el comando "joya_ev"
SHELL_CMD_REGISTER(ev, NULL, "Simular evento: ev <id_evento>", cmd_send_event);