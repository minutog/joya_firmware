# Mobile App BLE Handoff

This note is for Morgan and for any AI agent working in the production Joya app repository. The goal is to move the BLE connectivity slice from this prototype into the real app without changing the existing routine/emergency product flows.

The current Swift reference implementation lives in:

- `JoyaPhoneConnectionApp/JoyaPhoneConnection/JoyaBluetoothManager.swift`
- `JoyaPhoneConnectionApp/JoyaPhoneConnection/ContentView.swift`

Treat the Swift app as a reference for the BLE state machine and protocol mapping. Do not copy the prototype UI into the production app unless it is useful for a hidden debug screen.

## Current Firmware Contract

The latest firmware uses a custom Joya GATT service with binary commands. Do not use the older Nordic UART/text protocol (`PING`, `CLAIM`, `EVENT:...`, etc.) for this integration.

| Purpose | UUID | Direction |
| --- | --- | --- |
| Joya service | `a407e00a-00c1-464d-9173-2cb8be585343` | Advertised by Joya |
| TX characteristic | `a407e00a-00c1-464d-9173-2cb8be585344` | Joya notifies the app |
| RX characteristic | `a407e00a-00c1-464d-9173-2cb8be585345` | App writes commands to Joya |
| RX AUTH characteristic | `a407e00a-00c1-464d-9173-2cb8be585346` | App writes APP_ID during authentication |

The phone app is the BLE central. Joya is the BLE peripheral.

## Pairing And Reconnect Flow

1. User starts pairing/connection in the app.
2. App tells the user to double-click Joya.
3. App scans for the Joya service UUID.
4. App connects to the discovered peripheral.
5. App discovers the Joya service and the three characteristics above.
6. App subscribes to notifications on TX.
7. Firmware sends `0x05` (`ACK`) once notifications are enabled.
8. App writes the APP_ID as raw bytes to RX AUTH.
9. Firmware sends `0x05` (`ACK`) if APP_ID is accepted, or `0x06` (`NACK`) if rejected.
10. App marks the device as authenticated only after the APP_ID ACK.

After disconnects, Bluetooth toggles, app restarts, or firmware reboots, do not reuse old characteristic objects. Clear local BLE handles, scan again, reconnect, rediscover characteristics, subscribe to TX, and run the same APP_ID authentication flow.

## APP_ID Handling

The debug firmware currently expects exactly 5 raw bytes for `APP_ID`.

Implementation requirements:

- Generate the APP_ID once per app install/user/device relationship.
- Store it somewhere stable. For the prototype, `UserDefaults` is fine. For production, prefer Keychain or the app's secure storage layer.
- Send exactly 5 bytes for the current debug firmware.
- Do not send a UUID string, JSON, `CLAIM:...`, or any text wrapper.
- Keep `SIZE_APP_ID` easy to change because final firmware may move back to a longer value.

Reference behavior in Swift:

- `authAppIDSize = 5`
- Existing legacy UUIDs are normalized by removing dashes and taking the first 5 UTF-8 bytes.
- Short values are padded with ASCII `0`.

## Binary Commands

From Joya to the app:

| Byte | Name | Production app behavior |
| --- | --- | --- |
| `0x02` | `START_ROUTINE` | Turn on the existing routine/share mode. Reuse the production routine action, state, and backend path. |
| `0x03` | `END_ROUTINE` | Turn off/cancel the existing routine/share mode. |
| `0x04` | `EMERGENCY` | Turn on the existing emergency mode immediately, then write `0x41` back to RX. |
| `0x05` | `ACK` | During connection setup, advance the authentication state machine. After auth, log as command acknowledgement. |
| `0x06` | `NACK` | During connection setup, treat as APP_ID rejection and show a retry/re-pairing path. |

From the app to Joya:

| Byte | Name | When to send |
| --- | --- | --- |
| `0x41` | `ACK_EMERGENCY` | Every time the app receives `0x04` from Joya. |
| `0x42` | `STOP_EMERGENCY` | When the user/app cancels the active emergency in the production flow. |
| `0x43` | `FOLLOW_ME` | When the app wants Joya to play the "someone is coming/following" haptic. |
| `0x44` | `FRIEND_EMERGENCY` | When the backend/app tells this user that a friend has triggered an emergency. |

All command writes go to RX except APP_ID, which goes to RX AUTH.

## Integration Shape For The Production App

Add a device connection layer rather than putting CoreBluetooth directly into screens.

Recommended module shape:

- `JoyaDeviceManager` or equivalent central BLE service.
- Published/observable connection state: `idle`, `scanning`, `connecting`, `discovering`, `authenticating`, `connected`, `disconnected`, `failed`.
- Public commands: `startPairing()`, `reconnect()`, `disconnect()`, `sendStopEmergency()`, `sendFollowMe()`, `sendFriendEmergency()`.
- Event callbacks into existing product flows: `onRoutineStarted`, `onRoutineEnded`, `onEmergencyStarted`.
- Persistent storage for the stable APP_ID and, later, the selected Joya identity if needed.

Important boundaries:

- Keep BLE connection state separate from product emergency/routine state.
- Incoming `0x02` and `0x03` should call the same production code paths as tapping routine on/off in the app.
- Incoming `0x04` should call the same production code path as triggering emergency in the app, then immediately ACK Joya with `0x41`.
- Cancelling emergency in the app should send `0x42` to Joya after the production cancellation path starts or succeeds, depending on current product rules.
- If BLE is disconnected, the production app should still let backend/app emergency state behave correctly. BLE is an input/output channel, not the source of truth for the whole product.

## Things Not To Do

- Do not use the old text commands: `PING`, `CLAIM:<app_id>`, `EVENT:ROUTINE_START`, `ACK:EMERGENCY_ON`, `EMERGENCY_OFF`, etc.
- Do not consider the app connected/authenticated just because RX and TX were discovered.
- Do not send APP_ID through RX. It must go through RX AUTH.
- Do not generate a new APP_ID on every launch.
- Do not keep stale `CBPeripheral` characteristic references after disconnect.
- Do not make routine/emergency UI state depend only on BLE connection state.

## Suggested AI Prompt For Morgan's Repo

Use this prompt inside the production app repo:

```text
Integrate Joya BLE device connectivity using the firmware v2 binary GATT contract.

Add a dedicated device connection manager. The phone app is the BLE central and Joya is the peripheral. Scan for service a407e00a-00c1-464d-9173-2cb8be585343, discover TX a407e00a-00c1-464d-9173-2cb8be585344, RX a407e00a-00c1-464d-9173-2cb8be585345, and RX AUTH a407e00a-00c1-464d-9173-2cb8be585346. Subscribe to TX notifications. When Joya sends 0x05 after notifications are enabled, write the stable 5-byte APP_ID to RX AUTH. Mark the device authenticated only after the next 0x05. Treat 0x06 during setup as APP_ID rejection.

Map incoming bytes to the existing product flows: 0x02 starts routine mode, 0x03 ends routine mode, 0x04 starts emergency mode and must immediately write 0x41 to RX. Expose outgoing commands for existing app actions: 0x42 stops emergency, 0x43 triggers the follow-me/friend-coming haptic, and 0x44 triggers the friend-emergency haptic.

Keep BLE connection state separate from the app's routine/emergency product state. Clear and rediscover BLE handles after disconnect. Do not use the old NUS/text protocol.
```

## Reference Commit

The prototype app was updated for this contract in commit `5314bfa` (`Update app BLE protocol for firmware v2`).
