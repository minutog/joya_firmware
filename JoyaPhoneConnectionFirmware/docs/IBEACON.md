# Joya iBeacon identities

Both identities use one legacy, non-connectable advertising set. The firmware
selects the identity from the emergency phase only after a nonempty `app_id`
has been registered, while the connectable advertiser is active and Joya is
disconnected. Initial Setup and factory reset have no iBeacon. iOS Core
Location can monitor each UUID separately.

| Identity | Proximity UUID | Major | Minor |
| --- | --- | ---: | ---: |
| CONNECTION | `a407e00a-00c1-464d-9173-2cb8be585401` | 1 (`0x0001`) | 1 (`0x0001`) |
| EMERGENCY | `a407e00a-00c1-464d-9173-2cb8be585402` | 2 (`0x0002`) | 1 (`0x0001`) |

Manufacturer ID is Apple `0x004c`; iBeacon type and length are `0x02, 0x15`.
The measured-power byte is currently `0xc8` (-56 dBm) for both identities. It
is a placeholder and has not been calibrated for distance estimation.

## nRF Connect validation

Filter by Apple manufacturer data and decode the UUID, major and minor. Also
check that the usual connectable Joya advertisement is present wherever an
iBeacon is expected.

| Action | Expected iBeacon |
| --- | --- |
| Start initial Setup without an `app_id` | None; connectable advertising only |
| Register an `app_id`, then disconnect and restart advertising | CONNECTION |
| Start Setup advertising while disconnected with an `app_id` | CONNECTION |
| Triple click during initial Setup without an `app_id` | None; connectable advertising only |
| Triple click while disconnected with an `app_id` and connectable advertising active | EMERGENCY |
| Connect | None; emergency on a connected device uses GATT `0x04` |
| Triple click while connected | None; GATT `0x04` is sent and retried |
| Disconnect before ACK and wait for advertising restart | EMERGENCY |
| Reconnect before ACK | None; GATT `0x04` resumes after authentication |
| Disconnect again without ACK | EMERGENCY |
| Reconnect and send ACK `0x41`, then disconnect | CONNECTION |
| Trigger a new emergency, send STOP `0x42`, then disconnect | CONNECTION |
| Factory reset and start initial Setup again | None; connectable advertising only |
| Let ordinary Setup advertising time out without an emergency | None |

The existing emergency-specific timeout branch restarts connectable
advertising; it is not changed by this integration. The iBeacon follows that
advertising when it is active.
