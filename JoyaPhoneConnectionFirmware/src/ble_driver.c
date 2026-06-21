#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "ble_driver.h"
#include "app_state.h"
#include "app_comm.h"

extern struct k_msgq event_queue;

static struct bt_uuid_128 joya_svc_uuid = BT_UUID_INIT_128(BT_UUID_JOYA_SERVICE_VAL);
static struct bt_uuid_128 joya_tx_uuid  = BT_UUID_INIT_128(BT_UUID_JOYA_TX_VAL);
static struct bt_uuid_128 joya_rx_uuid  = BT_UUID_INIT_128(BT_UUID_JOYA_RX_VAL);

static struct bt_conn *current_conn = NULL;
static bool notify_enabled = false;



// --- Callbacks de Características GATT ---

static void on_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    event_type_t ev = value == BT_GATT_CCC_NOTIFY ? EV_BLE_NOTIFY_ENABLED : EV_BLE_NOTIFY_DISABLED;
    k_msgq_put(&event_queue, &ev, K_NO_WAIT);
    
    /*
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    if(notify_enabled) {
        LOG_INF("Notificaciones activadas por la App");
    } else {
        LOG_INF("Notificaciones desactivadas por la App");
    }
    LOG_INF("Notificaciones %s", notify_enabled ? "activadas" : "desactivadas");
    */
}

static ssize_t on_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 1) {
        LOG_WRN("Recibida longitud invalida: %d", len);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t val = ((uint8_t *)buf)[0];
    LOG_INF("App escribio el comando: 0x%02X", val);

    event_type_t ev;
    // Evaluamos el comando de la app (Ej: 0x0A = Detener Emergencia)
    if (val == COMMAND_STOP_EMERGENCY) {
        ev = EV_APP_CMD_STOP_EMERGENCY; // Asegurate que este enum exista en app_events.h
        k_msgq_put(&event_queue, &ev, K_NO_WAIT);
    } else if (val == COMMAND_ACK_EMERGENCY) {
        ev = EV_APP_ACK_EMERGENCY; // Asegurate que este enum exista en app_events.h
        k_msgq_put(&event_queue, &ev, K_NO_WAIT);
    } else if (val == COMMAND_FOLLOW_ME) {
        ev = EV_APP_CMD_FOLLOW_ME; // Asegurate que este enum exista en app_events.h
        k_msgq_put(&event_queue, &ev, K_NO_WAIT);
    } else {
        LOG_WRN("Comando desconocido recibido: 0x%02X", val);
    }
    
    return len;
}

// --- Definición de la Tabla GATT ---
BT_GATT_SERVICE_DEFINE(joya_svc,
    BT_GATT_PRIMARY_SERVICE(&joya_svc_uuid),
    
    // Característica TX: Soporta Notificaciones
    BT_GATT_CHARACTERISTIC(&joya_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(on_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    
    // Característica RX: Soporta Escritura
    BT_GATT_CHARACTERISTIC(&joya_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_rx_write, NULL)
);

// --- Paquetes de Advertising ---
/*static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, "Joya", 4), // Nombre genérico base
};*/

static const struct bt_data ad[] = {
    // Flags obligatorios de visibilidad BLE
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

// --- Callbacks de Conexión BLE ---
static void on_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Error de conexion: %d", err);
        return;
    }
    current_conn = bt_conn_ref(conn);
    LOG_INF("Telefono conectado!");

    // El driver NO evalúa lógicas, solo avisa a la FSM
    event_type_t ev = EV_BLE_CONNECTED; // Asegurate que este enum exista en app_events.h
    k_msgq_put(&event_queue, &ev, K_NO_WAIT);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    LOG_INF("Telefono desconectado (Razon: %d)", reason);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    notify_enabled = false;

    // Avisar a la FSM que perdimos enlace
    event_type_t ev = EV_BLE_DISCONNECTED; // Asegurate que este enum exista en app_events.h
    k_msgq_put(&event_queue, &ev, K_NO_WAIT);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

// --- API Pública ---


int ble_driver_init(void) {
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Error habilitando BT: %d", err);
        return err;
    }
    
    LOG_INF("Bluetooth inicializado con exito");
    return 0;
}

int ble_start_setup_advertising(void) {
    // 1. Cambiamos el nombre dinámicamente antes de encender la radio
    int name_err = bt_set_name("Joya Setup");
    if (name_err) {
        LOG_WRN("No se pudo setear el nombre dinámico: %d", name_err);
    }

    // 2. Iniciamos la radio diciéndole que incluya el nombre de forma nativa
    int err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Fallo al arrancar ADV de setup. Error: %d", err);
        return err;
    }

    LOG_INF("¡Publicidad de setup activa exitosamente!");
    return 0;
}


int ble_start_reconnect_advertising(void) {
    bt_set_name("Joya");
    int err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    LOG_INF("Iniciado ADV Reconnect: %d", err);
    return err;
}

int ble_stop_advertising(void) {
    return bt_le_adv_stop();
}

int ble_send_notify(uint8_t event_byte) {
    if (!current_conn || !notify_enabled) {
        LOG_WRN("No se puede notificar (Sin conexion o CCCD inactivo)");
        return -ENOTCONN;
    }

    // Buscamos el handle de nuestra característica TX
    const struct bt_gatt_attr *attr = bt_gatt_find_by_uuid(joya_svc.attrs, 0xFFFF, &joya_tx_uuid.uuid);
    if (!attr) {
        return -EINVAL;
    }

    int err = bt_gatt_notify(current_conn, attr, &event_byte, sizeof(event_byte));
    LOG_INF("Notificacion enviada: 0x%02X (Status: %d)", event_byte, err);
    return err;
}

// Asegúrate de que este wrapper verifique el estado del enlace
int ble_send_event_secure(uint8_t event_byte) {
    if (current_conn == NULL) {
        LOG_WRN("BLE desconectado, reintentando...");
        // Opcional: Aquí podrías pedirle a la FSM que pase a estado "Reconnecting" (to do)
        return -ENOTCONN;
    }
    return ble_send_notify(event_byte);
}


/*
// (30ms / 0.625 = 48) | (60ms / 0.625 = 96)
#define BT_LE_ADV_PARAM_AGGRESSIVE \
    BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME, \
                    48, 96, NULL)

// Función nueva para tu FSM
int ble_start_aggressive_advertising(void) {
    bt_set_name("Joya"); // Mantenemos el nombre de reconexión
    
    // Usamos el parámetro agresivo en vez del estándar
    int err = bt_le_adv_start(BT_LE_ADV_PARAM_AGGRESSIVE, ad, ARRAY_SIZE(ad), NULL, 0);
    
    LOG_INF("Iniciado ADV AGRESIVO: %d", err);
    return err;
}*/


void ble_force_reset(void) {
    // 1. Si hay una conexión activa, la desconectamos elegantemente
    if (current_conn) {
        int err = bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (err) {
            LOG_ERR("Fallo al desconectar: %d", err);
        } else {
            LOG_INF("Desconexión iniciada...");
        }
        // Nota: El stack disparará el callback on_disconnected automáticamente 
        // cuando la desconexión se complete, así que ahí es donde realmente 
        // limpiás el puntero 'current_conn' a NULL.
    }

    // 2. Frenamos cualquier advertising que pudiera estar corriendo
    bt_le_adv_stop();
    LOG_INF("Radio silenciada.");
}