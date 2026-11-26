# Recovery validation on nRF52 DK

Use a DK with the Joya board configuration, the existing MCUboot image, and a
working BLE OTA client. Keep the production source changes separate from the
temporary fault injection used in tests. Record reset reason (`RESETREAS`),
elapsed time since the last successful feed, BLE advertising, image state,
and whether the previous firmware can still be restored by OTA. Do not deploy
fault-injection images to assembled Joya units.

The production OTA artifact is `build/app/zephyr/zephyr.signed.bin` from a
sysbuild made with the signing key matching the deployed MCUboot. The merged
HEX and unsigned `zephyr.bin` are not OTA artifacts. The test image confirmation
and rollback policy in `test_image_guard.c` remains unchanged.

1. **FSM block:** In a DK-only image, insert a non-sleeping infinite loop at
   the start of `process_event()` for a chosen button event. Trigger it after
   boot. Confirm that the supervisor stops feeding and WDT resets the DK.
   Restore the source afterward.
2. **System workqueue block:** In a DK-only image, submit a work item whose
   handler loops forever. Confirm that the periodic guard cannot run and WDT
   resets the DK. Restore the source afterward.
3. **Fatal error:** In a DK-only image, trigger `k_oops()` after startup.
   Confirm immediate software reset. Inspect `RESETREAS` separately for
   software and WDT reset cases. MPU stack guards are deferred in this version.
4. **Long idle and low battery:** Leave the DK idle well beyond 30 seconds,
   then test logical suspension and the critical-battery idle/window states.
   Confirm no resets while workqueue and FSM are healthy. Check that button
   wake and the critical window still work.
5. **Normal and interrupted OTA:** Upload the signed image via BLE, including
   a transfer interrupted mid-upload. Reconnect and retry, and confirm the
   expected test-image confirmation or rollback. Include an upload spanning
   several watchdog periods and inspect flash/NVS behavior.
6. **Upgrade from a version without WDT:** Install an older signed image on
   the DK, then update by BLE OTA to the new image. Check first boot,
   confirmation, subsequent software reset, and OTA availability.
7. **Automatic rollback to a version without WDT:** Start from an older,
   confirmed image without WDT. Install the new WDT image as Test, but do not
   confirm it. Allow the existing test-image policy to reboot and MCUboot to
   restore the older image. The older application may inherit a running WDT
   after a soft reset and suffer one extra watchdog reset. Verify that it then
   remains stable for several watchdog periods and still accepts BLE OTA.
8. **Voluntary downgrade to a version without WDT:** Start from a confirmed
   WDT image and install an older no-WDT image as Test via BLE OTA. The inherited
   watchdog may expire before that Test image can be confirmed. Measure the
   actual timing and confirm whether the older image can be confirmed, remains
   stable, and continues accepting OTA. No downgrade-specific workaround is
   included in this version.
9. **Consecutive WDT-image updates:** Perform at least two BLE OTA updates
   between signed images that both use the same WDT configuration. Exercise
   both test-image and confirmed-image boots. Confirm no reset loop and that
   every image remains updateable.

The guard feeds only after `main()` finishes initialization and the system
workqueue runs. An idle FSM (`K_FOREVER` on an empty queue) is healthy. An
event taking over 15 seconds, or a nonempty queue with no completed event for
over 15 seconds after first observation, suppresses feeding. The 10-second
sampling interval makes detection time approximate. Long legitimate blocking
inside `process_event()` or system workqueue, including unusually slow flash
operations, may produce a false positive. A dead FSM with no pending events
cannot be distinguished from healthy idle by this guard.

If `button_init()` fails in a confirmed image, the application continues but
may have no way to start advertising: normal setup gestures depend on the
button. BLE OTA recovery is therefore not guaranteed in this case. If BLE
initialization fails, main never declares initialization complete, so the
supervisor never feeds; an active WDT expires while main sleeps. Without an
active WDT, main retries by software reset only after 30 seconds. A confirmed
image with persistently failing BLE initialization may still lack an OTA path.

WDT hardware configuration is fixed at 30 seconds, RR[0] only, running in
sleep, and paused when the debugger halts the CPU. CRV, RREN and CONFIG are
locked while WDT is active and retain their values across a soft reset. This
version rejects a running WDT with different register values and waits awake
for a hardware WDT reset. Future versions must keep these values or explicitly
implement compatibility with inherited configurations. Flexible acceptance
could be delivered later by application OTA, without changing MCUboot, after
DK validation of the transition.

Before enabling MPU and hardware stack protection in a later image, measure
peak stack use for the FSM, system workqueue, BT_RX and mcumgr_wq under BLE,
OTA and NVS load. Re-evaluate stack sizes, then validate flash erase/write,
NVS and OTA experimentally on the DK before deployment to assembled units.
Deferring MPU here is a precaution, not a finding that it is incorrect.
