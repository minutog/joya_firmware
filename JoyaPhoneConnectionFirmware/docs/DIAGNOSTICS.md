# GATT Diagnostics

The Diagnostics characteristic is in the existing Joya Configuration service
`a407e00a-00c1-464d-9173-2cb8be585200`.

| Characteristic | UUID | Properties | Permissions |
| --- | --- | --- | --- |
| Diagnostics | `a407e00a-00c1-464d-9173-2cb8be585202` | READ, NOTIFY | READ requires the current app-authenticated connection; CCC uses the existing read/write permissions. No characteristic WRITE. |

## One-byte and two-byte messages

READ returns two bytes: `A0 MM`. The same two bytes are notified once startup
has finished and both app authentication and Diagnostics notifications are
enabled. `MM` is a success mask; a clear bit means that initialization did
not return success. A fully successful normal boot returns `A0 1F`.

| Mask bit | Component | Success criterion |
| ---: | --- | --- |
| 0 (`0x01`) | BLE | `ble_driver_init() == 0`, including its OTA preparation and iBeacon setup |
| 1 (`0x02`) | Button | `button_init() == 0` |
| 2 (`0x04`) | Flash/Settings | `storage_init() == 0` |
| 3 (`0x08`) | Haptic | `haptics_init() == 0` |
| 4 (`0x10`) | Battery | `battery_init()` and `battery_monitor_start()` both return zero |

The original four-bit proposal omitted battery initialization. The second
byte allows it to be represented without conflating it with the four other
components. OTA initialization is included in BLE because its error propagates
through `ble_driver_init()`. A Test image can reboot on a button or BLE
initialization failure before a diagnostic connection is possible.

Runtime errors are one byte. They are emitted only if an app-authenticated
connection currently has Diagnostics notifications enabled. One pending error
may be overwritten by a newer one; failed notifications are dropped. Nothing
is stored for a future connection.

| Byte | Detection point |
| --- | --- |
| `E1` | `settings_save_one()` failed for app ID, emergency phase or name |
| `E2` | `settings_load()` failed; currently boot-only, so normally not transmitted |
| `E3` | `k_msgq_put()` failed for the FSM event queue |
| `E4` | BLE advertising, iBeacon HCI operation or command notification returned an error |
| `E5` | DRV2605 register I²C read/write returned an error |

## nRF Connect checks

1. Connect, discover the existing Configuration service, and verify that Name
   retains READ/WRITE and Diagnostics has READ/NOTIFY with no WRITE.
2. Before app authentication, READ Diagnostics: expect ATT authorization error.
3. Enable Diagnostics notifications, then enable command notifications and send
   the valid APP_ID. Expect one `A0 MM` notification after authentication.
4. READ Diagnostics again. Expect the same two bytes (normally `A0 1F`).
5. Disconnect and reconnect. No prior `Ex` message should be replayed; after
   authentication and enabling Diagnostics notifications, expect current `A0 MM`.
6. With an authenticated subscribed connection, induce a recoverable I²C or
   Settings save failure in a controlled test and expect the corresponding
   `Ex` notification. Normal behavior must continue if that notification fails.

Existing command, authentication, Name and battery UUIDs and characteristic
properties are unchanged. Adding Diagnostics can change numeric GATT handles;
clients should rediscover the service.
