# Joya connection iBeacon

Joya emits the connection iBeacon alongside connectable advertising when an
`APP_ID` is stored and the device is disconnected. Its UUID is
`a407e00a-00c1-464d-9173-2cb8be586001`; major and minor are both 1.
Manufacturer ID is Apple `0x004c`, iBeacon type and length are `0x02, 0x15`,
and measured power remains the uncalibrated `0xc8` (-56 dBm).

The iBeacon payload is independent of pending commands and the emergency phase.
Initial setup without an `APP_ID`, a connected session, factory reset, suspended
idle, and critical battery idle do not emit it. After a normal reboot with a
stored `APP_ID`, advertising starts automatically once a noncritical battery
level has been confirmed. A critical level at boot defers this until recovery.

The mobile app should monitor only this connection UUID, connect to Joya,
enable command TX notifications, and authenticate with `APP_ID`. Pending
commands are delivered over the existing GATT characteristic after that point.
An emergency (`0x04`) is retried until the app sends ACK (`0x41`); STOP (`0x42`)
retains its existing meaning. For a routine ON (`0x02`) or OFF (`0x03`), Joya
retains only the latest pending command and treats a successful
`bt_gatt_notify()` call as delivery. The app may miss a notification accepted
by BLE if the link drops, or receive a duplicate after a reboot between the
notification and clearing the persisted pending state. There is no routine ACK.
The stored routine is tied to the APP_ID that generated it; a different APP_ID
after factory reset or firmware rollback cannot receive the old command.

## Validation with nRF Connect

Check that both connectable advertising and the connection iBeacon are visible
after a configured, noncritical reboot and after a normal disconnect. Triggering
an emergency must leave the iBeacon UUID unchanged. Verify that neither
advertisement appears during critical idle or suspension, and that initial
setup without an `APP_ID` has only connectable advertising after the setup
gesture.
