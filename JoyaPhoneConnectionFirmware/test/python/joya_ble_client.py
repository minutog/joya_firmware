from bleak import BleakScanner, BleakClient
import logging
from enum import IntEnum
import asyncio


logger = logging.getLogger(__name__)


DEVICE_NAME = "Joya"
DEVICE_INIT_NAME = "Joya Setup"

JOYA_SERVICE_UUID = "a407e00a-00c1-464d-9173-2cb8be585343"
TX_UUID = "a407e00a-00c1-464d-9173-2cb8be585344"
RX_UUID = "a407e00a-00c1-464d-9173-2cb8be585345"
RX_AUTH_UUID = "a407e00a-00c1-464d-9173-2cb8be585346"


class JoyaCommand(IntEnum):
    INIT_JOYA_COMMAND = 0x01
    COMMAND_ROUTINE = 0x02
    COMMAND_END_ROUTINE = 0x03
    COMMAND_EMERGENCY = 0x04
    COMMAND_ACK = 0x05
    COMMAND_NACK = 0x06
    END_JOYA_COMMAND = 0x07


class AppCommand(IntEnum):
    INIT_APP_COMMAND = 0x40
    COMMAND_ACK_EMERGENCY = 0x41
    COMMAND_STOP_EMERGENCY = 0x42
    COMMAND_FOLLOW_ME = 0x43
    COMMAND_FRIEND_EMERGENCY = 0x44
    END_APP_COMMAND = 0x45


class JoyaBleClient:
    def __init__(self):
        self.device = None
        self.client = None
        self.notification_queue = asyncio.Queue()

    @property
    def is_connected(self) -> bool:
        return (
            self.client is not None
            and self.client.is_connected
        )

    def _check_connection(self):
        if not self.is_connected:
            raise RuntimeError("Device is not connected")

    async def scan(
        self,
        initial: bool = True,
        timeout: float = 5.0
    ):
        device_name = (
            DEVICE_INIT_NAME
            if initial
            else DEVICE_NAME
        )

        logger.info("Scanning for %s", device_name)

        self.device = await BleakScanner.find_device_by_name(
            device_name,
            timeout=timeout
        )

        if self.device is None:
            raise RuntimeError(
                f"Device '{device_name}' not found"
            )

        logger.info(
            "Found device: %s (%s)",
            self.device.name,
            self.device.address
        )

        return self.device

    async def connect(self):
        if self.device is None:
            raise RuntimeError(
                "No device selected. Call scan() first."
            )

        logger.info(
            "Connecting to %s (%s)",
            self.device.name,
            self.device.address
        )

        self.client = BleakClient(self.device)

        await self.client.connect()

        if not self.client.is_connected:
            raise RuntimeError("Connection failed")

        logger.info(
            "Connected to %s (%s)",
            self.device.name,
            self.device.address
        )

    async def disconnect(self):
        if self.client is None:
            return

        if self.client.is_connected:
            await self.client.disconnect()

        logger.info("Disconnected")

        self.client = None

    def list_gatt(self):
        self._check_connection()

        logger.info(
            "Listing GATT services and characteristics"
        )

        for service in self.client.services:
            logger.info(
                "Service: %s",
                service.uuid
            )

            for characteristic in service.characteristics:
                logger.info(
                    "  Characteristic: %s | properties=%s",
                    characteristic.uuid,
                    characteristic.properties
                )

    def validate_gatt(self):
        self._check_connection()

        service = self.client.services.get_service(
            JOYA_SERVICE_UUID
        )

        if service is None:
            raise RuntimeError(
                f"Joya service not found: "
                f"{JOYA_SERVICE_UUID}"
            )

        logger.info("Joya service found")

        required = {
            TX_UUID: "notify",
            RX_UUID: "write",
            RX_AUTH_UUID: "write",
        }

        for uuid, required_property in required.items():
            characteristic = service.get_characteristic(
                uuid
            )

            if characteristic is None:
                raise RuntimeError(
                    f"Characteristic not found: {uuid}"
                )

            if (
                required_property
                not in characteristic.properties
            ):
                raise RuntimeError(
                    f"Characteristic {uuid} does not "
                    f"support '{required_property}'"
                )

            logger.info(
                "Characteristic OK: %s",
                uuid
            )

    def _notification_handler(
        self,
        sender,
        data
    ):
        data = bytes(data)

        logger.info(
            "Notification received: %s",
            data.hex(" ")
        )

        self.notification_queue.put_nowait(data)

    async def subscribe_tx(self):
        self._check_connection()

        await self.client.start_notify(
            TX_UUID,
            self._notification_handler
        )

        logger.info(
            "Subscribed to TX notifications"
        )

    async def unsubscribe_tx(self):
        self._check_connection()

        await self.client.stop_notify(
            TX_UUID
        )

        logger.info(
            "Unsubscribed from TX notifications"
        )

    def clear_notifications(self):
        while not self.notification_queue.empty():
            self.notification_queue.get_nowait()

        logger.info(
            "Notification queue cleared"
        )

    async def wait_for_message(
        self,
        timeout: float = 5.0
    ) -> bytes:
        try:
            return await asyncio.wait_for(
                self.notification_queue.get(),
                timeout=timeout
            )

        except asyncio.TimeoutError:
            raise RuntimeError(
                f"No notification received within "
                f"{timeout} s"
            )

    async def expect_no_message(
        self,
        timeout: float = 3.0
    ):
        try:
            data = await asyncio.wait_for(
                self.notification_queue.get(),
                timeout=timeout
            )

        except asyncio.TimeoutError:
            return

        command = self.parse_joya_command(
            data
        )

        raise RuntimeError(
            f"Unexpected command received: "
            f"{command.name}"
        )

    async def send_app_id(
        self,
        app_id: bytes
    ):
        self._check_connection()

        if len(app_id) != 16:
            raise ValueError(
                f"App ID must be 16 bytes, "
                f"got {len(app_id)}"
            )

        logger.info(
            "Sending App ID: %s",
            app_id.hex(" ")
        )

        await self.client.write_gatt_char(
            RX_AUTH_UUID,
            app_id,
            response=True
        )

    async def send_app_command(
        self,
        command: AppCommand
    ):
        self._check_connection()

        logger.info(
            "Sending App command: %s (0x%02X)",
            command.name,
            command.value
        )

        await self.client.write_gatt_char(
            RX_UUID,
            bytes([command.value]),
            response=True
        )

    def parse_joya_command(
        self,
        data: bytes
    ) -> JoyaCommand:
        if len(data) == 0:
            raise RuntimeError(
                "Received empty notification"
            )

        try:
            return JoyaCommand(
                data[0]
            )

        except ValueError:
            raise RuntimeError(
                f"Unknown Joya command: "
                f"0x{data[0]:02X}"
            )

    async def wait_for_command(
        self,
        expected_command: JoyaCommand,
        timeout: float = 5.0
    ):
        data = await self.wait_for_message(
            timeout
        )

        command = self.parse_joya_command(
            data
        )

        logger.info(
            "Received command: %s (0x%02X)",
            command.name,
            command.value
        )

        if command != expected_command:
            raise RuntimeError(
                f"Expected {expected_command.name}, "
                f"received {command.name}"
            )