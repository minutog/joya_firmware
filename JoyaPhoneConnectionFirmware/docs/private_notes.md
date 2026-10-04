- Revisar prioridades de vibración (emergencia > a todo)

--
> TODO — Prioridad para vibraciones pendientes

Actualmente, mientras el NBM5100 espera la señal RDY, solo se guarda un patrón háptico pendiente. Si llega otra solicitud durante ese intervalo, puede reemplazar la anterior.

A futuro, implementar un único patrón pendiente con prioridades:

- Factory reset y emergencia: prioridad máxima.
- Emergencia de un amigo: prioridad alta.
- Cancelación, follow-me y ACK: prioridad media.
- Inicio de rutina y setup: prioridad baja.

El nuevo patrón solo debe reemplazar al pendiente si tiene mayor prioridad. No reiniciar el contador de intentos ni el timeout cuando llegue otra solicitud.

No utilizar una cola FIFO completa: podría reproducir vibraciones atrasadas que ya no representen el estado actual.
--

- Revisar retries en esos casos

- Anuncio continuo luego de desconexión y luego del apagado -> ver tiempo máximo
- Agresivo y luego paso a más tiempo (por si la desconexión es accidental)
- Repetir el FRIEND_EMERGENCY cada 5 segundos hasta que haga el recibido -> ya agregado el COMMAND_FRIEND_STOP_EMERGENCY como 0x45

---

- Objetivo general: ningún fallo de BLE debería dejar la Joya en un estado sin recuperación física. Como OTA es el único mecanismo de actualización, siempre debería quedar disponible al menos un camino por botón para volver a un estado recuperable.

- Factory reset como escape global: hoy es el mecanismo más fuerte porque se procesa con prioridad, fuerza STATE_UNPAIRED, borra almacenamiento e intenta desconectar. Mientras el botón funcione, permite salir de casi cualquier estado de la FSM.

- STATE_BONDED_DISCONNECTED: si el advertising de reconexión falla y no hay retry, el dispositivo queda desconectado y el doble clic no tiene efecto en ese estado. Sigue siendo recuperable por factory reset, pero conviene agregar retry automático de reconexión.

- Estados de handshake (WAITING_NOTIFICATION_ENABLE, SETUP_WAITING_IDENTIFIER): si la conexión queda trabada y el protocolo no avanza, el doble clic tampoco recupera el equipo. Factory reset sí. Conviene revisar si hace falta timeout de protocolo para volver a un estado estable.

- Fallo de bt_enable() al boot: es el caso más peligroso detectado. Actualmente puede terminar main() antes de button_init(), dejando al equipo sin BLE y sin botón; ahí no habría OTA, doble clic ni factory reset por botón. Esto conviene corregir para que el botón se inicialice incluso si BLE falla

- Regla de diseño propuesta:
  fallo normal de protocolo → retry / doble clic
  fallo de estado o asociación → factory reset
  fallo de BLE en boot → botón debe seguir disponible

- Dos mejoras prioritarias de robustez:
  1. Retry automático de advertising desde STATE_BONDED_DISCONNECTED.
  2. No permitir que un fallo de bt_enable() impida inicializar el botón.

---
# Fallos en ADV
- doble clic fallido → STATE_UNPAIRED; 
- emergencia sin APP_ID → UNPAIRED si falla; 
- desconexión con APP_ID → BONDED_DISCONNECTED;
- desconexión desde authenticated → BONDED_DISCONNECTED;

---
# Pines
- P0.06: SCL
- P0.07: SDA
- P0.08: HAPT_EN
- P0.11: RDY
- P0.12: START
- P0.13: BUTTON