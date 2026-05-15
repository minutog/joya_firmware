# Joya Firmware

Firmware and test app workspace for the Joya BLE safety wristband prototype.

## Hardware Target

- Nordic nRF52832-QFAA-T
- CR1620 coin cell battery
- 10 mm x 2 mm haptic actuator
- 1 physical button

## Repository Layout

- `JoyaFirmware/` - Zephyr/Nordic NCS firmware used to flash the PCB and test BLE pairing/reconnect.
- `JoyaFirmwareTest/` - iOS demo app workspace. Later we will evolve this into the Flux-based test app path.
- `ReferenciaInicial.c` - readable product-state reference/pseudocode.
- `PreviousTest/` - earlier hardware test reference. Build outputs are intentionally ignored.
- `docs/` - protocol notes and firmware/app connection design.

## Current Firmware Goal

The current firmware is a first BLE connection test, not the final safety-product firmware. It focuses on:

- Advertising as `Joya Setup` while unclaimed.
- Accepting a simple app-level `CLAIM:<app_id>` command over Nordic UART Service.
- Saving `claimed = true` in flash.
- Advertising as `Joya` after claim.
- Restarting advertising after disconnect so the app can reconnect automatically.

Important BLE model: Joya is the BLE peripheral. The phone app is the BLE central. That means the firmware advertises; the phone performs the actual reconnect.

## Build

From `JoyaFirmware/`:

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
```

See `docs/ble_pairing_logic.md` for the connection model.
