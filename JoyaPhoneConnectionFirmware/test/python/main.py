import asyncio
import logging

from joya_ble_client import JoyaBleClient
from joya_test_runner import JoyaTestRunner


logging.basicConfig(
    level=logging.INFO,
    format=(
        "%(asctime)s | "
        "%(levelname)s | "
        "%(message)s"
    )
)


TEST_APP_ID = bytes.fromhex(
    "00 01 02 03 04 05 06 07 "
    "08 09 0A 0B 0C 0D 0E 0F"
)

WRONG_APP_ID = bytes.fromhex(
    "10 11 12 13 14 15 16 17 "
    "18 19 1A 1B 1C 1D 1E 1F"
)


# Firmware values:
#
# INITIAL    = 1000 ms
# FAST       = 2000 ms
# MEDIUM     = 5000 ms
# SLOW       = 10000 ms
# VERY SLOW  = 30000 ms

EMERGENCY_INTERVALS = [
    1.0,
    2.0,
    5.0,
    10.0,
    30.0,
]

EMERGENCY_RETRY_TOLERANCE = 0.5

BLE_SETUP_TIMEOUT = 90.0


def show_menu(
    tests
):
    print()
    print("=" * 60)
    print("JOYA FIRMWARE TEST RUNNER")
    print("=" * 60)

    for option, test_info in tests.items():
        name, _ = test_info

        print(
            f"{option:>2}. {name}"
        )

    print()
    print(" A. Run all tests")
    print(" 0. Exit")
    print()


async def run_test(
    name,
    test
):
    print()
    print(
        f"Running: {name}"
    )

    try:
        return await test()

    except Exception as e:
        print()
        print(
            f"[FAIL] Unexpected error: {e}"
        )

        return False


async def run_all(
    tests
):
    print()
    print("=" * 60)
    print("RUNNING FULL TEST SEQUENCE")
    print("=" * 60)

    passed = 0
    failed = 0

    for name, test in tests.values():
        result = await run_test(
            name,
            test
        )

        if result:
            passed += 1

        else:
            failed += 1

            print()

            answer = input(
                "Test failed. Continue? [y/N]: "
            ).strip().lower()

            if answer != "y":
                break

    print()
    print("=" * 60)
    print("TEST SUMMARY")
    print("=" * 60)

    print(
        f"Passed: {passed}"
    )

    print(
        f"Failed: {failed}"
    )


async def main():
    joya = JoyaBleClient()

    runner = JoyaTestRunner(
        joya=joya,
        app_id=TEST_APP_ID,
        wrong_app_id=WRONG_APP_ID
    )

    tests = {
        # -----------------------------------------------------
        # Setup / authentication
        # -----------------------------------------------------

        "1": (
            "Fresh device advertising name",
            runner.test_fresh_setup_name
        ),

        "2": (
            "Provision App ID",
            runner.test_provisioning
        ),

        "3": (
            "Provisioned advertising name",
            runner.test_provisioned_name
        ),

        "4": (
            "Valid App ID",
            runner.test_valid_app_id
        ),

        "5": (
            "Wrong App ID / NACK",
            runner.test_wrong_app_id
        ),

        # -----------------------------------------------------
        # Normal operation
        # -----------------------------------------------------

        "10": (
            "Routine start",
            runner.test_routine_start
        ),

        "11": (
            "Routine end",
            runner.test_routine_end
        ),

        "12": (
            "Factory reset",
            runner.test_factory_reset
        ),

        # -----------------------------------------------------
        # Emergency
        # -----------------------------------------------------

        "20": (
            "Emergency start",
            runner.test_emergency_start
        ),

        "21": (
            "Emergency retry cycle",
            lambda: runner.test_emergency_retry_cycle(
                expected_intervals=EMERGENCY_INTERVALS,
                tolerance=EMERGENCY_RETRY_TOLERANCE
            )
        ),

        "22": (
            "Emergency retry cycle reset",
            lambda: runner.test_emergency_restart_cycle(
                first_interval=EMERGENCY_INTERVALS[0],
                tolerance=EMERGENCY_RETRY_TOLERANCE
            )
        ),

        "23": (
            "Emergency ACK stops retries",
            lambda: runner.test_emergency_ack_stops_retries(
                silence_timeout=2.0
            )
        ),

        "24": (
            "Routine blocked during emergency",
            runner.test_routine_blocked_during_emergency
        ),

        "25": (
            "Stop emergency",
            runner.test_stop_emergency
        ),

        "26": (
            "Emergency after power cycle",
            runner.test_emergency_power_cycle
        ),

        "27": (
            "Emergency after BLE disconnect",
            runner.test_emergency_ble_reconnection
        ),
    }

    try:
        while True:
            show_menu(
                tests
            )

            option = input(
                "Select test: "
            ).strip().upper()

            if option == "0":
                break

            if option == "A":
                await run_all(
                    tests
                )

                continue

            if option not in tests:
                print(
                    "[ERROR] Invalid option."
                )

                continue

            name, test = tests[
                option
            ]

            await run_test(
                name,
                test
            )

            input(
                "\nPress ENTER to return "
                "to the menu..."
            )

    finally:
        await joya.disconnect()

    print()
    print(
        "Test runner closed."
    )


if __name__ == "__main__":
    asyncio.run(main())