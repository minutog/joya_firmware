import time
import asyncio

from joya_ble_client import (
    JoyaBleClient,
    JoyaCommand,
    AppCommand,
)


class JoyaTestRunner:
    def __init__(
        self,
        joya: JoyaBleClient,
        app_id: bytes,
        wrong_app_id: bytes
    ):
        self.joya = joya
        self.app_id = app_id
        self.wrong_app_id = wrong_app_id

    # ---------------------------------------------------------
    # UI HELPERS
    # ---------------------------------------------------------

    def _show_test(
        self,
        name: str
    ):
        print()
        print("=" * 60)
        print(name)
        print("=" * 60)

    def _action_then_continue(
        self,
        message: str
    ):
        print()
        print(f"[ACTION] {message}")
        input("Press ENTER when done...")

    def _ready_then_action(
        self,
        message: str
    ):
        print()
        input("Press ENTER when ready...")
        print(f"[ACTION] {message}")

    def _pass(
        self,
        message: str
    ):
        print()
        print(f"[PASS] {message}")

    def _fail(
        self,
        message: str
    ):
        print()
        print(f"[FAIL] {message}")

    # ---------------------------------------------------------
    # CONNECTION HELPERS
    # ---------------------------------------------------------

    async def _disconnect_if_needed(self):
        if self.joya.is_connected:
            await self.joya.disconnect()

    async def _connect_provisioned(
        self,
        timeout: float = 5.0
    ):
        await self._disconnect_if_needed()

        await self.joya.scan(
            initial=False,
            timeout=timeout
        )

        await self.joya.connect()

        self.joya.validate_gatt()

        await self.joya.subscribe_tx()

        self.joya.clear_notifications()

        await self.joya.send_app_id(
            self.app_id
        )

        await self.joya.wait_for_command(
            JoyaCommand.COMMAND_ACK,
            timeout=timeout
        )

    async def _stop_emergency(
        self,
        timeout: float = 5.0
    ):
        self.joya.clear_notifications()

        await self.joya.send_app_command(
            AppCommand.COMMAND_STOP_EMERGENCY
        )

        await self.joya.wait_for_command(
            JoyaCommand.COMMAND_ACK,
            timeout=timeout
        )

    # ---------------------------------------------------------
    # SETUP / AUTHENTICATION
    # ---------------------------------------------------------

    async def test_fresh_setup_name(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "FRESH DEVICE ADVERTISING NAME"
        )

        await self._disconnect_if_needed()

        self._action_then_continue(
            "Turn Joya OFF and ON."
        )

        self._action_then_continue(
            "Enter setup mode using the double press."
        )

        start = time.monotonic()

        try:
            await self.joya.scan(
                initial=True,
                timeout=timeout
            )

            elapsed = time.monotonic() - start

        except Exception as e:
            self._fail(str(e))
            return False

        self._pass(
            f"'Joya Setup' found after "
            f"{elapsed:.2f} s."
        )

        return True

    async def test_provisioning(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "APP ID PROVISIONING"
        )

        await self._disconnect_if_needed()

        self._action_then_continue(
            "Turn Joya OFF and ON."
        )

        self._action_then_continue(
            "Enter setup mode using the double press."
        )

        try:
            await self.joya.scan(
                initial=True,
                timeout=timeout
            )

            await self.joya.connect()
            self.joya.validate_gatt()
            await self.joya.subscribe_tx()

            self.joya.clear_notifications()

            await self.joya.send_app_id(
                self.app_id
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_ACK,
                timeout=timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "App ID stored and ACK received."
        )

        await self._disconnect_if_needed()

        return True

    async def test_provisioned_name(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "PROVISIONED DEVICE ADVERTISING NAME"
        )

        await self._disconnect_if_needed()

        self._action_then_continue(
            "Turn Joya OFF and ON."
        )

        self._action_then_continue(
            "Enter setup mode using the double press."
        )

        start = time.monotonic()

        try:
            await self.joya.scan(
                initial=False,
                timeout=timeout
            )

            elapsed = time.monotonic() - start

        except Exception as e:
            self._fail(str(e))
            return False

        self._pass(
            f"'Joya' found after "
            f"{elapsed:.2f} s."
        )

        return True

    async def test_valid_app_id(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "VALID APP ID"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "Valid App ID accepted."
        )

        await self._disconnect_if_needed()

        return True

    async def test_wrong_app_id(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "WRONG APP ID"
        )

        await self._disconnect_if_needed()

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self.joya.scan(
                initial=False,
                timeout=timeout
            )

            await self.joya.connect()
            self.joya.validate_gatt()
            await self.joya.subscribe_tx()

            self.joya.clear_notifications()

            await self.joya.send_app_id(
                self.wrong_app_id
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_NACK,
                timeout=timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "Wrong App ID correctly rejected."
        )

        await self._disconnect_if_needed()

        return True

    # ---------------------------------------------------------
    # ROUTINE
    # ---------------------------------------------------------

    async def test_routine_start(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "ROUTINE START"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Perform ONE short press now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_ROUTINE,
                timeout=timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "COMMAND_ROUTINE received."
        )

        await self._disconnect_if_needed()

        return True

    async def test_routine_end(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "ROUTINE END"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Perform a LONG PRESS now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_END_ROUTINE,
                timeout=timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "COMMAND_END_ROUTINE received."
        )

        await self._disconnect_if_needed()

        return True


    async def test_factory_reset(
        self,
        hold_time: float = 15.0,
        scan_timeout: float = 5.0
    ) -> bool:
        self._show_test("FACTORY RESET")

        await self._disconnect_if_needed()

        try:
            # -----------------------------------------------------
            # Verify initial state: provisioned device -> "Joya"
            # -----------------------------------------------------

            self._action_then_continue(
                "Turn Joya ON."
            )

            self._action_then_continue(
                "Enter connection mode using the double press."
            )

            print()
            print("[STEP] Checking that the device is provisioned...")

            await self.joya.scan(
                initial=False,
                timeout=scan_timeout
            )

            self._pass(
                "Provisioned device found as 'Joya'."
            )

            # -----------------------------------------------------
            # Factory reset
            # -----------------------------------------------------

            print()
            print(
                "[ACTION] Start holding the Factory Reset button."
            )

            input(
                "Press ENTER as soon as you start holding it..."
            )

            print()
            print(
                f"[STEP] Keep holding for approximately "
                f"{hold_time:.0f} seconds."
            )

            start = time.monotonic()

            while True:
                elapsed = time.monotonic() - start
                remaining = hold_time - elapsed

                if remaining <= 0:
                    break

                print(
                    f"\rHold... {remaining:4.1f} s remaining",
                    end="",
                    flush=True
                )

                await asyncio.sleep(0.1)

            print()
            print()
            print(">>> RELEASE THE BUTTON NOW <<<")
            print("\a", end="", flush=True)

            input(
                "Press ENTER after releasing the button..."
            )

            # Give firmware some time to finish the reset.
            await asyncio.sleep(1.0)

            # -----------------------------------------------------
            # Verify state after factory reset
            # -----------------------------------------------------

            self._action_then_continue(
                "Perform the double press to enter setup mode."
            )

            print()
            print("[STEP] Looking for 'Joya Setup'...")

            await self.joya.scan(
                initial=True,
                timeout=scan_timeout
            )

            self._pass(
                "'Joya Setup' found."
            )

            # -----------------------------------------------------
            # Negative check: "Joya" should no longer be advertised
            # -----------------------------------------------------

            print()
            print(
                "[STEP] Checking that 'Joya' is no longer advertised..."
            )

            try:
                await self.joya.scan(
                    initial=False,
                    timeout=2.0
                )

            except RuntimeError:
                # This is exactly what we expect:
                # "Joya" should not be found.
                pass

            else:
                raise RuntimeError(
                    "Factory reset seems incomplete: "
                    "'Joya' is still being advertised."
                )

        except Exception as e:
            self._fail(str(e))
            return False

        self._pass(
            "Factory reset successful: "
            "'Joya Setup' found and 'Joya' not found."
        )

        return True

    # ---------------------------------------------------------
    # EMERGENCY
    # ---------------------------------------------------------

    async def test_emergency_start(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "EMERGENCY START"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Trigger the emergency now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            await self._stop_emergency(
                timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "COMMAND_EMERGENCY received."
        )

        await self._disconnect_if_needed()

        return True

    async def test_emergency_retry_cycle(
        self,
        expected_intervals: list[float],
        tolerance: float = 0.5,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "EMERGENCY RETRY CYCLE"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Trigger the emergency now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            previous_time = time.monotonic()

            for index, expected in enumerate(
                expected_intervals,
                start=1
            ):
                await self.joya.wait_for_command(
                    JoyaCommand.COMMAND_EMERGENCY,
                    timeout=expected + tolerance + 1.0
                )

                now = time.monotonic()
                measured = now - previous_time
                previous_time = now

                print(
                    f"[MEASURE] Retry {index}: "
                    f"{measured:.2f} s "
                    f"(expected {expected:.2f} s)"
                )

                if abs(measured - expected) > tolerance:
                    raise RuntimeError(
                        f"Retry {index}: expected "
                        f"{expected:.2f} s, "
                        f"measured {measured:.2f} s"
                    )

            await self._stop_emergency(
                timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "Emergency retry timing is correct."
        )

        await self._disconnect_if_needed()

        return True

    async def test_emergency_restart_cycle(
        self,
        first_interval: float,
        tolerance: float = 0.5,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "EMERGENCY RETRY RESET"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Trigger the emergency now."
            )

            # Initial emergency
            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            # First retry
            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=first_interval + tolerance + 1.0
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Trigger the emergency AGAIN now."
            )

            # Restart sends immediately
            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            restart_time = time.monotonic()

            # Must restart from first interval
            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=first_interval + tolerance + 1.0
            )

            measured = (
                time.monotonic() - restart_time
            )

            print(
                "[MEASURE] First retry after restart: "
                f"{measured:.2f} s "
                f"(expected {first_interval:.2f} s)"
            )

            if abs(measured - first_interval) > tolerance:
                raise RuntimeError(
                    "Emergency retry sequence did not "
                    "restart from the first interval."
                )

            await self._stop_emergency(
                timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "Emergency retry sequence restarted "
            "from the first interval."
        )

        await self._disconnect_if_needed()

        return True

    async def test_emergency_ack_stops_retries(
        self,
        silence_timeout: float = 2.0,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "EMERGENCY ACK STOPS RETRIES"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Trigger the emergency now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            self.joya.clear_notifications()

            await self.joya.send_app_command(
                AppCommand.COMMAND_ACK_EMERGENCY
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_ACK,
                timeout=timeout
            )

            self.joya.clear_notifications()

            print(
                f"[STEP] Waiting {silence_timeout:.1f} s "
                "to verify retries have stopped..."
            )

            await self.joya.expect_no_message(
                timeout=silence_timeout
            )

            # ACK does not end the emergency.
            await self._stop_emergency(
                timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "ACK received and emergency retries stopped."
        )

        await self._disconnect_if_needed()

        return True

    async def test_routine_blocked_during_emergency(
        self,
        silence_timeout: float = 3.0,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "ROUTINE BLOCKED DURING EMERGENCY"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Trigger the emergency now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            # Stop retries but keep emergency active.
            self.joya.clear_notifications()

            await self.joya.send_app_command(
                AppCommand.COMMAND_ACK_EMERGENCY
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_ACK,
                timeout=timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Perform ONE short press now."
            )

            await self.joya.expect_no_message(
                timeout=silence_timeout
            )

            print(
                "[PASS] Routine start was ignored."
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Perform a LONG PRESS now."
            )

            await self.joya.expect_no_message(
                timeout=silence_timeout
            )

            print(
                "[PASS] Routine end was ignored."
            )

            await self._stop_emergency(
                timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "Routine controls remained blocked "
            "while emergency was active."
        )

        await self._disconnect_if_needed()

        return True

    async def test_stop_emergency(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "STOP EMERGENCY"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Trigger the emergency now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            self.joya.clear_notifications()

            await self.joya.send_app_command(
                AppCommand.COMMAND_STOP_EMERGENCY
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_ACK,
                timeout=timeout
            )

            # Functional check:
            # normal operation must work again.
            self.joya.clear_notifications()

            self._ready_then_action(
                "Perform ONE short press now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_ROUTINE,
                timeout=timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "Emergency stopped and normal "
            "routine operation restored."
        )

        await self._disconnect_if_needed()

        return True

    async def test_emergency_power_cycle(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "EMERGENCY AFTER POWER CYCLE"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Trigger the emergency now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            await self.joya.disconnect()

            self._action_then_continue(
                "Turn Joya OFF and ON."
            )

            self._action_then_continue(
                "Enter connection mode using "
                "the double press."
            )

            await self._connect_provisioned(
                timeout
            )

            # Emergency was persisted.
            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            await self._stop_emergency(
                timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "Emergency persisted across power cycle "
            "and resumed after authentication."
        )

        await self._disconnect_if_needed()

        return True

    async def test_emergency_ble_reconnection(
        self,
        timeout: float = 5.0
    ) -> bool:
        self._show_test(
            "EMERGENCY AFTER BLE DISCONNECTION"
        )

        self._action_then_continue(
            "Turn Joya ON and enter connection mode "
            "using the double press."
        )

        try:
            await self._connect_provisioned(
                timeout
            )

            self.joya.clear_notifications()

            self._ready_then_action(
                "Trigger the emergency now."
            )

            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            print()
            print(
                "[STEP] Disconnecting Bluetooth "
                "from the PC..."
            )

            await self.joya.disconnect()

            start = time.monotonic()

            # Joya should advertise automatically.
            await self._connect_provisioned(
                timeout
            )

            elapsed = time.monotonic() - start

            # Authentication must restart emergency.
            await self.joya.wait_for_command(
                JoyaCommand.COMMAND_EMERGENCY,
                timeout=timeout
            )

            await self._stop_emergency(
                timeout
            )

        except Exception as e:
            self._fail(str(e))
            await self._disconnect_if_needed()
            return False

        self._pass(
            "Emergency resumed after BLE reconnection "
            f"({elapsed:.2f} s)."
        )

        await self._disconnect_if_needed()

        return True