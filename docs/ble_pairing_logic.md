# Joya BLE Pairing Logic

This document describes the first firmware/app connection model for the prototype.

## Roles

- Joya: BLE peripheral.
- Phone app: BLE central.

Joya does not actively reconnect to the phone. Joya restarts connectable advertising, and the phone app scans/reconnects.

## Stored Data

On Joya:

- `claimed`: whether this Joya has already been claimed by an app.
- `app_id`: temporary prototype identifier sent by the app.
- BLE bond keys: intentionally disabled in this prototype path.

On the phone:

- Joya peripheral identifier.
- Joya id returned by the firmware.
- Later: app secret in Keychain/Keystore.
- BLE bond keys: intentionally disabled in this prototype path.

## Prototype Flow

```text
Boot
  |
  |-- claimed=false -> advertise as "Joya Setup" for setup
  |
  |-- claimed=true  -> advertise as "Joya" for reconnect

Phone connects
  |
  |-- APP -> JOYA: PING
  |-- JOYA -> APP: PONG:claimed=0|1

If unclaimed:
  |
  |-- APP -> JOYA: CLAIM:<app_id>
  |-- JOYA saves claimed=true
  |-- JOYA -> APP: CLAIM_OK:JOYA-DEV-001

Disconnect
  |
  |-- if claimed=true: advertise again as "Joya"
  |-- if claimed=false and setup window is active: advertise as "Joya Setup"
```

## Security Notes

This prototype firmware intentionally does not enable BLE bonding. Reconnect is kept simple: Joya advertises the NUS service, the phone connects as a central, and the app-level `CLAIM:<app_id>` decides whether setup is already complete.

For production, we should add a stricter app-level authorization step:

```text
APP -> JOYA: HELLO + nonce
JOYA -> APP: challenge
APP -> JOYA: HMAC(challenge, app_secret)
JOYA: accepts commands only after verification
```

That app secret should be created during claim and stored in Joya flash plus iOS Keychain / Android Keystore.

## Re-Pairing

Re-pairing should not be a normal reboot. It should be a deliberate factory reset gesture on the physical button. After reset:

- Delete `claimed`.
- Delete `app_id`.
- No BLE bond deletion is needed in this prototype path.
- Return to setup advertising.
