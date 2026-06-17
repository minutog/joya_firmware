# Firmware/App Integration Guide

This guide explains how the Joya firmware prototype and the phone app prototype talk to each other. It is meant for both the firmware developer and the app developer, so each flow calls out what each side owns.

The current repo contains:

- `JoyaPhoneConnectionFirmware/`: Zephyr/Nordic NCS firmware for the Joya phone-connection slice.
- `JoyaPhoneConnectionApp/`: a Swift/CoreBluetooth iOS test app used to validate the BLE flow.
- The final mobile app may be implemented in Expo/iOS, but it should keep the same BLE roles, service UUIDs, message names, and state rules unless both teams agree to change the contract.

## One Rule To Keep In Mind

Joya is the BLE peripheral. The phone app is the BLE central.

That means:

- Firmware advertises and exposes the Nordic UART Service.
- The app scans, connects, discovers characteristics, subscribes to notifications, and writes commands.
- Joya does not actively reconnect to the phone. On reboot or disconnect, Joya advertises again. The app is responsible for scanning and reconnecting.

## BLE Transport

The prototype uses Nordic UART Service (NUS) as a simple text-message pipe.

| Purpose | UUID | Direction |
| --- | --- | --- |
| NUS service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | Advertised by Joya |
| RX characteristic | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | App writes to Joya |
| TX characteristic | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Joya notifies app |

Messages are UTF-8 text commands. The current firmware command buffer is small, so keep commands short. `CLAIM:<app_id>` should use an `app_id` of 32 characters or less.

BLE bonding is intentionally disabled in this prototype. The current link between a Joya and an app is the app-level `CLAIM:<app_id>` stored in Joya flash.

## First-Time Pairing

Goal: take a new or reset Joya from "unclaimed" to "claimed by this app".

### Firmware side

1. On boot, load `claimed` and `app_id` from Zephyr settings.
2. If `claimed=false`, do not advertise forever.
3. Wait for a double click.
4. On double click, play the pairing wake haptic and advertise as `Joya Setup` for 90 seconds.
5. Accept a BLE connection from the phone.
6. When the app subscribes to TX notifications, send `HELLO:JOYA:claimed=0`.
7. Reply to `PING` with `PONG:claimed=0`.
8. On `CLAIM:<app_id>`, save `app_id` and `claimed=true` in flash.
9. Send `CLAIM_OK:JOYA-DEV-001`.
10. Stop treating the device as setup-only; future boots advertise as claimed/reconnect.

If the Joya is already claimed and receives another `CLAIM:<app_id>`, firmware returns `ERR:ALREADY_CLAIMED`.

### App side

1. User starts the connection flow.
2. Tell the user to double click Joya.
3. Scan for the NUS service UUID.
4. Connect to the discovered peripheral.
5. Discover the NUS service and RX/TX characteristics.
6. Subscribe to TX notifications.
7. Send `PING` once per connection setup.
8. If Joya answers `PONG:claimed=0`, send `CLAIM:<app_id>`.
9. Treat `CLAIM_OK:<joya_id>` as pairing success.
10. Store enough local state to recognize this app identity later. In the current Swift test app, `app_id` is stored in `UserDefaults`.

The current Swift test app intentionally scans instead of relying on an old saved CoreBluetooth peripheral id. The final app can use a different storage strategy, but reconnect should still work by scanning for the Joya service and completing the same handshake.

### Pairing sequence

```text
User double-clicks Joya
Joya advertises as "Joya Setup" for 90s
App scans for NUS service
App connects
App discovers RX/TX
App subscribes to TX notifications
Joya -> App: HELLO:JOYA:claimed=0
App -> Joya: PING
Joya -> App: PONG:claimed=0
App -> Joya: CLAIM:<app_id>
Joya saves claimed=true and app_id
Joya -> App: CLAIM_OK:JOYA-DEV-001
App marks Joya connected/paired
```

## Reconnect After Distance, Bluetooth Toggle, Or App Restart

Goal: after pairing once, the user should not have to understand BLE. Joya and the app should find each other again.

### Firmware side

- If `claimed=true` on boot, advertise as `Joya`.
- After any disconnect, restart connectable advertising.
- If claimed, advertise as `Joya`.
- If unclaimed and the setup window is still open, advertise as `Joya Setup`.
- If unclaimed and the setup window has expired, stop advertising and wait for another double click.

### App side

- When Bluetooth is powered on, be ready to scan.
- When the connection drops unexpectedly, clear the old RX/TX characteristic references and scan again.
- On reconnect, rediscover NUS and resubscribe to notifications.
- Send `PING` once per connection setup.
- If Joya answers `PONG:claimed=1`, treat it as an already-claimed reconnect.
- Do not resend `CLAIM` unless Joya says `PONG:claimed=0`.

### Reconnect sequence

```text
Connection drops
Joya clears current connection
Joya advertises as "Joya"
App receives disconnect callback
App starts scanning again
App connects
App discovers RX/TX and subscribes
Joya -> App: HELLO:JOYA:claimed=1
App -> Joya: PING
Joya -> App: PONG:claimed=1
App marks Joya connected
```

## Reboot Behavior

### Firmware reboot

On firmware reboot:

- Settings are loaded from flash.
- If `claimed=true`, Joya advertises for reconnect immediately.
- If `claimed=false`, Joya waits for a double click before advertising setup.
- `claimed` and `app_id` survive reboot.
- The current prototype does not persist the emergency latch across reboot.

Firmware developer note: decide with the app developer whether production emergency state must survive a firmware reboot. The mock currently focuses on connection mechanics, not full product safety persistence.

### App reboot

On app reboot:

- Recreate the BLE manager.
- Wait for Bluetooth to be powered on.
- Start the normal scan/connect/discover/subscribe flow.
- Reuse the same app identity if available.
- Send `PING` after the NUS channel is ready.

App developer note: the app should treat the BLE peripheral object and characteristics as disposable. After an app restart or disconnect, rediscover them instead of assuming stale handles still work.

## Button-To-App Messages

The firmware owns button timing and haptic feedback. The app owns product meaning, UI state, and network-side actions.

| User action on Joya | Firmware behavior | Message to app | App behavior |
| --- | --- | --- | --- |
| Single click while connected and not in emergency | Play routine haptic | `EVENT:ROUTINE_START` | Mark routine/share flow active or send routine event upstream |
| Hold for at least 900ms while connected and not in emergency | Play cancel haptic | `EVENT:ROUTINE_CANCEL` | Cancel routine/share flow |
| Double click while disconnected | Open advertising | None | App should find Joya by scanning |
| Double click while connected | Ignore | None | No app action |
| Triple click in any state | Start emergency, vibrate immediately | `EVENT:EMERGENCY_ON` | Enter emergency state and reply `ACK:EMERGENCY_ON` |
| 15s hold | Clear phone pairing | `EVENT:PHONE_PAIRING_RESET` if connected | Forget local pairing state and return to setup flow |

Single clicks while disconnected are ignored. Routine clicks are not queued for later delivery.

## Emergency Delivery

Emergency is the highest-priority flow.

### Firmware side

1. Triple click starts emergency from any state, even disconnected.
2. Joya vibrates immediately.
3. Joya sends `EVENT:EMERGENCY_ON` if the app is connected and subscribed.
4. If there is no connected/subscribed app, Joya keeps the emergency pending and advertises.
5. Until `ACK:EMERGENCY_ON` arrives, retry delivery on this schedule:
   - 1s
   - 2s
   - 5s
   - 10s
   - then every 30s
6. After `ACK:EMERGENCY_ON`, stop retrying, but keep emergency active.
7. Emergency clears only when the app sends `EMERGENCY_OFF` or `CANCEL_EMERGENCY`.

### App side

1. On `EVENT:EMERGENCY_ON`, enter emergency state.
2. Send `ACK:EMERGENCY_ON` every time that event is received.
3. Keep emergency UI/product state active until the user or backend flow cancels it.
4. To clear emergency on Joya, send `EMERGENCY_OFF`.
5. Expect `ACK:EMERGENCY_OFF`.

Important coordination point: the current Swift test app clears its local activity state on disconnect. For the final product, the app and firmware should agree whether an already-acknowledged emergency must be restored after app reboot or reconnect. If yes, add an explicit emergency status message during handshake.

## App-To-Joya Messages

These are the current app commands.

| App command | Firmware response | Meaning |
| --- | --- | --- |
| `PING` | `PONG:claimed=0` or `PONG:claimed=1` | Handshake/status check |
| `CLAIM:<app_id>` | `CLAIM_OK:JOYA-DEV-001`, `ERR:ALREADY_CLAIMED`, or `ERR:CLAIM_SAVE_FAILED` | First-time app-level claim |
| `ACK:EMERGENCY_ON` | No text response | Confirms app received the emergency event |
| `EMERGENCY_OFF` | `ACK:EMERGENCY_OFF` | Clear emergency |
| `CANCEL_EMERGENCY` | `ACK:EMERGENCY_OFF` | Alias for clearing emergency |
| `FRIEND_COMING` | `ACK:FRIEND_COMING_FOR_YOU` | Play friend-coming haptic |
| `FRIEND_COMING_FOR_YOU` | `ACK:FRIEND_COMING_FOR_YOU` | Alias for friend-coming haptic |
| `CANCEL_ROUTINE` | `ACK:CANCEL_ROUTINE` | App-side routine cancel acknowledgement path |
| `HAPTIC_TEST` | `ACK:HAPTIC_TEST` | Test haptic pattern |
| Unknown command | `ERR:UNKNOWN_COMMAND` | Command not supported |

Product rule: `FRIEND_COMING` represents "a friend is following/coming for you" and should be sent by the app only during an active emergency. The current firmware tolerates the message and restores emergency state from the phone before playing the haptic. For production, both teams should decide whether firmware should reject this command outside emergency.

## Firmware-To-App Messages

These are the current firmware messages.

| Firmware message | App should do |
| --- | --- |
| `HELLO:JOYA:claimed=0` | Start handshake; send `PING` if not already sent |
| `HELLO:JOYA:claimed=1` | Start handshake; send `PING` if not already sent |
| `PONG:claimed=0` | Send `CLAIM:<app_id>` |
| `PONG:claimed=1` | Treat as connected/reconnected |
| `CLAIM_OK:<joya_id>` | Mark pairing successful |
| `ERR:ALREADY_CLAIMED` | If app just sent `CLAIM`, treat as connected for the mock; for production, decide stricter ownership rules |
| `ERR:CLAIM_SAVE_FAILED` | Show setup failure and let user retry |
| `ERR:UNKNOWN_COMMAND` | Log/report protocol mismatch |
| `EVENT:ROUTINE_START` | Start or repeat routine/share flow |
| `EVENT:ROUTINE_CANCEL` | Cancel routine/share flow |
| `EVENT:EMERGENCY_ON` | Enter emergency and send `ACK:EMERGENCY_ON` |
| `EVENT:PHONE_PAIRING_RESET` | Forget local pairing state and return to unpaired UI |
| `ACK:HAPTIC_TEST` | Log test success |
| `ACK:CANCEL_ROUTINE` | Log routine cancel command acknowledgement |
| `ACK:EMERGENCY_OFF` | Treat Joya emergency state as cleared |
| `ACK:FRIEND_COMING_FOR_YOU` | Log friend-coming haptic delivery |

## Pairing Reset

Goal: deliberately forget the app/Joya relationship and allow setup again.

### Firmware side

1. User holds the button for 15 seconds.
2. Firmware sends `EVENT:PHONE_PAIRING_RESET` if connected.
3. Firmware clears `claimed` and `app_id` from settings.
4. Firmware plays the reset haptic.
5. Firmware disconnects from the phone.
6. Firmware stops advertising and waits for a new double click.

### App side

1. On `EVENT:PHONE_PAIRING_RESET` or disconnect after user-visible reset, forget local Joya association.
2. Return to unpaired/setup UI.
3. Do not reconnect automatically as if nothing happened.
4. Wait for the user to start pairing again.

## What Each Developer Should Own

### Firmware developer

- Keep BLE peripheral role and NUS UUIDs stable unless the app developer signs off.
- Keep advertising names stable: `Joya Setup` for setup, `Joya` for claimed reconnect.
- Persist `claimed` and `app_id`.
- Own button gesture detection, haptic patterns, emergency retry timing, and pairing reset.
- Send only documented text events unless the app developer has added support.
- Restart advertising after disconnect so the app can reconnect.

### App developer

- Keep BLE central role.
- Scan for the NUS service UUID.
- Discover RX/TX and subscribe to TX notifications on every new connection.
- Make setup handshake idempotent: one `PING`, one `CLAIM` only when needed.
- Own UI/product state for connected, routine, emergency, disconnected, and setup failure.
- Always ACK `EVENT:EMERGENCY_ON`.
- Send `EMERGENCY_OFF` when emergency is cancelled in the app.
- Send `FRIEND_COMING` only when emergency is active, unless both teams change that product rule.

## Open Decisions Before Production

These are not blockers for the mock, but both teams should align before the final firmware/app contract is frozen.

- Should BLE bonding or an app-level challenge/response replace the simple `CLAIM:<app_id>` model?
- Should emergency state survive firmware reboot?
- Should an acknowledged emergency be re-announced to the app after reconnect?
- Should `FRIEND_COMING` be rejected by firmware when emergency is not active?
- What should the app show if it finds a Joya that is already claimed by a different app?
- Should `joya_id` be fixed, generated at manufacturing, or stored in flash?
