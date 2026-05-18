# Joya Phone Connection

BLE phone-connection firmware and test app workspace for the Joya safety wristband prototype.

## Hardware Target

- Nordic nRF52832-QFAA-T
- CR1620 coin cell battery
- 10 mm x 2 mm haptic actuator
- 1 physical button

## Repository Layout

- `JoyaPhoneConnectionFirmware/` - Zephyr/Nordic NCS firmware focused only on phone pairing, claiming, reconnect, and button event transport.
- `JoyaPhoneConnectionApp/` - iOS demo app workspace for testing the phone connection flow. Later we can evolve this into the Flux-based test app path.
- `ReferenciaInicial.c` - readable product-state reference/pseudocode.
- `PreviousTest/` - earlier hardware test reference. Build outputs are intentionally ignored.
- `docs/` - protocol notes and firmware/app connection design.

## Current Scope

This is not the full safety-product firmware. It is the isolated Joya-phone connection slice, so Julieta can build the rest of the firmware without confusing this test harness with the final product logic. It focuses on:

- Advertising as `Joya Setup` while unclaimed.
- Accepting a simple app-level `CLAIM:<app_id>` command over Nordic UART Service.
- Saving `claimed = true` in flash.
- Advertising as `Joya` after claim.
- Restarting advertising after disconnect so the app can reconnect automatically.

Important BLE model: Joya is the BLE peripheral. The phone app is the BLE central. That means the firmware advertises; the phone performs the actual reconnect.

## Nota Para Julieta

Este repo deja armada solamente la base de conexion entre Joya y el celular. La parte importante para reutilizar es:

- Joya espera un doble click para abrir el advertising inicial de setup.
- La app encuentra el dispositivo por el servicio BLE, sin que el usuario elija de una lista.
- La app hace un `CLAIM:<app_id>` y Joya guarda ese claim en flash.
- Una vez claimed, Joya puede volver a anunciarse como `Joya` para que el celular se reconecte.
- La comunicacion actual usa Nordic UART Service porque es simple para prototipar mensajes de ida y vuelta.

Los eventos de `ROUTINE_START`, `ROUTINE_CANCEL` y `EMERGENCY_START` son mock de prueba. Estan ahi solo para confirmar que:

- El boton del PCB llega al firmware.
- El firmware puede enviar eventos a la app.
- La app puede mostrar estados basicos y mandar comandos de cancelacion.

No tomar esos eventos como decisiones finales de producto. Todavia quedan abiertas para el firmware final decisiones como cantidad real de clicks, duracion de hold, debounce definitivo, estados de seguridad, escalamiento de emergencia, patrones hapticos, consumo/bateria, reset de fabrica, bloqueo contra otros celulares y cualquier logica real de rutina o emergencia.

En resumen: de este trabajo conviene tomar como base la conexion Joya-phone, el claim inicial y el reconnect. La logica de seguridad/rutina/emergencia esta representada solo como mock para testear el canal.

## Reglas Del Boton En Este Prototipo

Tiempos actuales:

- Debounce: 50 ms.
- Ventana de clicks: 600 ms despues de cada click corto.
- Hold corto: 900 ms, usado por ahora solo como mock `ROUTINE_CANCEL` mientras esta conectado.
- Hold de reset: 15 segundos en cualquier estado.

Comportamiento:

- Desconectado + un click: no hace nada.
- Desconectado + doble click: abre BLE advertising para setup/reconnect.
- Desconectado + triple click: activa emergencia, vibra y reintenta avisar al telefono cuando reconecte.
- Conectado + un click: manda mock `EVENT:ROUTINE_START`, pero solo despues de que cierre la ventana de 600 ms.
- Conectado + doble click: no hace nada.
- Conectado + triple click: activa emergencia, vibra y manda `EVENT:EMERGENCY_ON`; no manda rutina primero.
- Emergencia activa: ignora gestos normales hasta recibir `EMERGENCY_OFF` desde el telefono.
- Cualquier estado + hold de 15 segundos: desconecta, borra el app claim, frena advertising y espera un nuevo doble click como setup fresco.

## Patrones Hapticos En Este Prototipo

Los patrones de boton usan el DRV2605 en modo RTP para poder controlar intensidad por pasos. Si llega un nuevo evento, el nuevo patron interrumpe el anterior.

- Doble click para pairing: rampa exponencial ascendente de 4 segundos, de 0 a intensidad maxima.
- Un click rutina: `tu-tu  tu  tuu`, como confirmacion corta.
- Hold para apagar rutina: rampa exponencial descendente de 4 segundos, de intensidad maxima a 0.
- Triple click emergencia: latido `tu-tu tu-tu`, 1 segundo de silencio, `tu-tu tu-tu`.
- `FRIEND_COMING_FOR_YOU` desde el telefono: vibracion larga y dos pulsos cortos.

El reset de pairing por hold de 15 segundos conserva un efecto corto separado porque es una accion tecnica de liberacion del dispositivo, no una decision final de UX de seguridad.

## Build

From `JoyaPhoneConnectionFirmware/`:

```sh
./scripts/west_build.sh
```

Default board:

```sh
nrf52dk/nrf52832
```

## Flash

List connected probes:

```sh
./scripts/list_probes.sh
```

Flash through the nRF52 DK debug-out probe:

```sh
./scripts/west_flash.sh <DK_SERIAL_NUMBER>
```

## BLE Test Protocol

The first app can use Nordic UART Service:

- Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX, app writes to Joya: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX, app subscribes to notifications: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

Basic messages:

```text
APP -> JOYA: PING
JOYA -> APP: PONG:claimed=0|1

APP -> JOYA: CLAIM:<app_id>
JOYA -> APP: CLAIM_OK:JOYA-DEV-001
JOYA -> APP: ERR:ALREADY_CLAIMED

JOYA -> APP: EVENT:EMERGENCY_ON
APP -> JOYA: ACK:EMERGENCY_ON
APP -> JOYA: FRIEND_COMING_FOR_YOU
APP -> JOYA: EMERGENCY_OFF
```

See `docs/ble_pairing_logic.md` for the connection model.
