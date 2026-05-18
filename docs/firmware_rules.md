# Firmware Rules

Short version of the current Joya firmware behavior.

## BLE Connection

- Joya is the BLE peripheral. The phone is the BLE central.
- Joya never connects to the phone by itself. It advertises; the phone scans and connects.
- If Joya is already claimed, it advertises as `Joya` on boot for reconnect.
- If Joya is not claimed, setup is user-gated: press double click to advertise as `Joya Setup`.
- Setup advertising lasts `90s`. If no phone connects, advertising stops.
- After disconnect, Joya advertises again:
  - claimed: `Joya`
  - unclaimed: `Joya Setup` only while setup window is open

## Button Timing

- Debounce: `50ms`.
- Click window: `600ms`.
- Routine cancel hold: `900ms`.
- Pairing reset hold: `15s`.

## Routine

- Routine controls only work while the phone is connected and emergency is not active.
- 1 click: `EVENT:ROUTINE_START` + routine haptic.
- Hold `>=900ms`: `EVENT:ROUTINE_CANCEL` + cancel haptic.
- Double click while connected is ignored.
- While disconnected, 1 click is ignored and 2 clicks open BLE advertising.

## Emergency

Emergency has priority over everything else.

- Triple click starts emergency in any state, even disconnected.
- Joya vibrates immediately on triple click. It does not wait for the phone ACK.
- Joya sends `EVENT:EMERGENCY_ON` to the phone.
- Until the phone replies `ACK:EMERGENCY_ON`, Joya retries:
  - `1s`
  - `2s`
  - `5s`
  - `10s`
  - then every `30s`
- After `ACK:EMERGENCY_ON`, Joya stops retrying but stays in emergency.
- Emergency only turns off when the phone sends `EMERGENCY_OFF` or `CANCEL_EMERGENCY`.
- While emergency is active:
  - routine clicks and holds are ignored
  - triple click still vibrates again
  - if the emergency was already ACKed, triple click does not resend to the phone
  - if it was not ACKed yet, triple click restarts the retry path

## Friend Coming

- Phone can send `FRIEND_COMING` or `FRIEND_COMING_FOR_YOU`.
- Joya vibrates the friend-coming pattern.
- Joya replies `ACK:FRIEND_COMING_FOR_YOU`.
- This does not start emergency retries.

## Pairing Reset

- Hold button for `15s`.
- Joya clears `claimed` and `app_id`.
- Joya disconnects from the phone.
- Joya waits for a new double click to open setup again.

