# nRF52832 custom PCB BLE button peripheral

This project builds firmware for an nRF52832 target and sends BLE messages when a button on `P0.24` is pressed.

## What it does
- Configures `P0.24` as GPIO input with pull-up and active-low logic.
- Enables GPIO interrupt on button press (`edge to active`).
- Advertises as `Joya Button PCB`.
- Exposes Nordic UART Service (NUS):
  - Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
  - TX (notify) UUID: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- Sends payload `"1"` on each button press (matches your iOS parser for `button1`).
- Keeps RTT logs enabled for debugging in VS Code.

## Hardware wiring (DK as programmer/debug probe)
- DK `SWDIO` -> custom PCB `SWDIO`
- DK `SWDCLK` -> custom PCB `SWDCLK`
- DK `RESET` -> custom PCB `RESET`
- DK `GND` -> custom PCB `GND`
- DK `VDD` -> custom PCB `VDD` (or power target separately, but keep grounds common)

Use the nRF52 DK in **Debug Out** mode so the on-board debugger programs the external target.

## Build and flash in VS Code
Before building, verify these paths in `.vscode/settings.json`:
- `joya.ncsRoot`
- `joya.ncsToolchainBin`
- `joya.appSymlink`
- `joya.board`

1. Run task: `List connected nRF probes`
2. Copy the DK serial number from the terminal output.
3. Run task: `Flash firmware (DK -> custom PCB)`
4. Enter the DK serial when prompted.

## Test with your iOS app
1. Flash this firmware to the custom PCB.
2. Open the iOS app and scan for devices.
3. Connect to `Joya Button PCB`.
4. Press the physical button connected to `P0.24`.
5. The app should receive `"1"` and classify it as `Boton 1`.

## Debug with RTT in VS Code
1. Open **Run and Debug**.
2. Start `Debug + RTT console (DK -> custom PCB)`.
3. Press the button connected to `P0.24`.
4. Observe logs such as:
   - `BLE connected: ...`
   - `NUS notify enabled`
   - `Button press sent over BLE: 1`

## CLI equivalents
```sh
./scripts/west_build.sh
./scripts/west_flash.sh <DK_SERIAL_NUMBER>
```
