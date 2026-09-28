#include <Arduino.h>
#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * Simulates both haptic devices with a classic dual-controller ESP32.
 *
 * Wiring (one physical I2C bus):
 *
 * DK P0.26 SDA ----+---- ESP32 GPIO21 (NBM SDA)
 *                  +---- ESP32 GPIO25 (DRV SDA)
 *
 * DK P0.27 SCL ----+---- ESP32 GPIO22 (NBM SCL)
 *                  +---- ESP32 GPIO26 (DRV SCL)
 *
 * DK GND ---------------- ESP32 GND
 *
 * Add one 4.7 kohm pull-up from SDA to 3.3 V
 * and one from SCL to 3.3 V.
 *
 * Connect the DK's NBM RDY input directly to 3.3 V as planned.
 *
 * This requires a classic ESP32 with Wire and Wire1.
 * It is not intended for single-I2C-controller variants such as ESP32-C3.
 */

static constexpr uint8_t NBM_ADDRESS = 0x2E;
static constexpr uint8_t DRV_ADDRESS = 0x5A;

static constexpr int NBM_SDA = 21;
static constexpr int NBM_SCL = 22;

static constexpr int DRV_SDA = 25;
static constexpr int DRV_SCL = 26;

static constexpr uint32_t I2C_HZ = 100000;


/* ============================================================
 * NBM5100 registers and COMMAND bits
 * ============================================================ */

static constexpr uint8_t NBM_STATUS  = 0x00;
static constexpr uint8_t NBM_PROFILE = 0x07;
static constexpr uint8_t NBM_COMMAND = 0x08;
static constexpr uint8_t NBM_SET1    = 0x09;
static constexpr uint8_t NBM_SET2    = 0x0A;
static constexpr uint8_t NBM_SET3    = 0x0B;
static constexpr uint8_t NBM_SET4    = 0x0C;

static constexpr uint8_t NBM_EOD = 0x01;
static constexpr uint8_t NBM_ECM = 0x02;
static constexpr uint8_t NBM_ACT = 0x04;


/* ============================================================
 * DRV2605 registers and modes used by the firmware
 * ============================================================ */

static constexpr uint8_t DRV_STATUS = 0x00;
static constexpr uint8_t DRV_MODE   = 0x01;
static constexpr uint8_t DRV_RTP    = 0x02;

static constexpr uint8_t DRV_INTERNAL_TRIGGER = 0x00;
static constexpr uint8_t DRV_RTP_MODE         = 0x05;
static constexpr uint8_t DRV_STANDBY          = 0x40;
static constexpr uint8_t DRV_STANDBY_INTERNAL_TRIGGER =
    DRV_STANDBY | DRV_INTERNAL_TRIGGER;


/* ============================================================
 * Logging
 * ============================================================ */

enum class DeviceId : uint8_t {
    NBM,
    DRV
};

enum class Operation : uint8_t {
    WRITE,
    READ
};

struct LogEvent {
    uint32_t timestamp_ms;
    DeviceId device;
    Operation operation;
    uint8_t reg;
    uint8_t value;
};

static QueueHandle_t log_queue;
static volatile uint32_t dropped_logs;


/* ============================================================
 * Simulated registers
 * ============================================================ */

static uint8_t nbm_registers[256];
static uint8_t drv_registers[256];

static uint8_t nbm_pointer;
static uint8_t drv_pointer;


/* ============================================================
 * Vibration tracking
 * ============================================================ */

static bool vibration_active;
static uint32_t vibration_number;
static uint32_t vibration_start_ms;


/* ============================================================
 * Logging helpers
 * ============================================================ */

static void queue_log(DeviceId device,
                      Operation operation,
                      uint8_t reg,
                      uint8_t value)
{
    LogEvent event = {
        millis(),
        device,
        operation,
        reg,
        value
    };

    if (log_queue == nullptr ||
        xQueueSend(log_queue, &event, 0) != pdTRUE) {
        dropped_logs++;
    }
}


/* ============================================================
 * Generic I2C slave handling
 * ============================================================ */

static void receive_transaction(TwoWire &bus,
                                int byte_count,
                                DeviceId device,
                                uint8_t *registers,
                                uint8_t &pointer)
{
    uint8_t data[32];
    size_t length = 0;

    while (bus.available() && length < sizeof(data)) {
        data[length++] = static_cast<uint8_t>(bus.read());
    }

    while (bus.available()) {
        (void)bus.read();
    }

    if (byte_count <= 0 || length == 0) {
        return;
    }

    pointer = data[0];

    /*
     * One byte only selects the register
     * for a following read.
     */
    for (size_t index = 1; index < length; index++) {
        uint8_t reg = pointer++;

        registers[reg] = data[index];

        queue_log(device,
                  Operation::WRITE,
                  reg,
                  data[index]);
    }
}


static void send_register(TwoWire &bus,
                          DeviceId device,
                          uint8_t *registers,
                          uint8_t &pointer)
{
    uint8_t reg = pointer++;
    uint8_t value = registers[reg];

    bus.write(value);

    queue_log(device,
              Operation::READ,
              reg,
              value);
}


/* ============================================================
 * NBM callbacks
 * ============================================================ */

static void on_nbm_receive(int count)
{
    receive_transaction(Wire,
                        count,
                        DeviceId::NBM,
                        nbm_registers,
                        nbm_pointer);
}


static void on_nbm_request()
{
    send_register(Wire,
                  DeviceId::NBM,
                  nbm_registers,
                  nbm_pointer);
}


/* ============================================================
 * DRV callbacks
 * ============================================================ */

static void on_drv_receive(int count)
{
    receive_transaction(Wire1,
                        count,
                        DeviceId::DRV,
                        drv_registers,
                        drv_pointer);
}


static void on_drv_request()
{
    send_register(Wire1,
                  DeviceId::DRV,
                  drv_registers,
                  drv_pointer);
}


/* ============================================================
 * Register names
 * ============================================================ */

static const char *nbm_name(uint8_t reg)
{
    switch (reg) {
        case NBM_STATUS:
            return "STATUS";

        case NBM_PROFILE:
            return "PROFILE";

        case NBM_COMMAND:
            return "COMMAND";

        case NBM_SET1:
            return "SET1";

        case NBM_SET2:
            return "SET2";

        case NBM_SET3:
            return "SET3";

        case NBM_SET4:
            return "SET4";

        default:
            return "REGISTER";
    }
}


static const char *drv_name(uint8_t reg)
{
    switch (reg) {
        case DRV_STATUS:
            return "STATUS";

        case DRV_MODE:
            return "MODE";

        case DRV_RTP:
            return "RTP_INPUT";

        default:
            return "REGISTER";
    }
}


/* ============================================================
 * NBM log printing
 * ============================================================ */

static void print_nbm(const LogEvent &event)
{
    Serial.printf(
        "[%8lu ms] NBM %c %-8s [0x%02X] %s 0x%02X",
        static_cast<unsigned long>(event.timestamp_ms),
        event.operation == Operation::WRITE ? 'W' : 'R',
        nbm_name(event.reg),
        event.reg,
        event.operation == Operation::WRITE ? "<-" : "->",
        event.value
    );

    /*
     * SET2:
     * ICH     -> bits 7:5
     * VDHHIZ  -> bit 4
     * VMIN    -> bits 2:0
     */
    if (event.operation == Operation::WRITE &&
        event.reg == NBM_SET2) {

        Serial.printf(
            "  [ICH=%u VDHHIZ=%u VMIN=%u]%s",
            (event.value >> 5) & 0x07,
            (event.value >> 4) & 0x01,
            event.value & 0x07,
            event.value == 0x34
                ? " OK"
                : " UNEXPECTED (want 0x34)"
        );
    }

    if (event.operation == Operation::WRITE &&
        event.reg == NBM_COMMAND) {

        bool eod = (event.value & NBM_EOD) != 0;
        bool ecm = (event.value & NBM_ECM) != 0;
        bool act = (event.value & NBM_ACT) != 0;

        Serial.printf(
            "  [EOD=%u ECM=%u ACT=%u]",
            eod,
            ecm,
            act
        );

        if (ecm && act) {
            vibration_active = true;
            vibration_start_ms = event.timestamp_ms;
            vibration_number++;

            Serial.printf(
                " <<< VIBRATION %lu START",
                static_cast<unsigned long>(vibration_number)
            );

        } else if (ecm && !act) {

            if (vibration_active) {
                Serial.printf(
                    " <<< VIBRATION %lu END after %lu ms",
                    static_cast<unsigned long>(vibration_number),
                    static_cast<unsigned long>(
                        event.timestamp_ms -
                        vibration_start_ms
                    )
                );
            }

            vibration_active = false;
        }
    }

    Serial.println();
}


/* ============================================================
 * DRV log printing
 * ============================================================ */

static void print_drv(const LogEvent &event)
{
    Serial.printf(
        "[%8lu ms] DRV %c %-9s [0x%02X] %s 0x%02X",
        static_cast<unsigned long>(event.timestamp_ms),
        event.operation == Operation::WRITE ? 'W' : 'R',
        drv_name(event.reg),
        event.reg,
        event.operation == Operation::WRITE ? "<-" : "->",
        event.value
    );

    if (event.operation == Operation::WRITE &&
        event.reg == DRV_MODE) {

        if (event.value == DRV_RTP_MODE) {
            Serial.print(" [RTP mode, awake]");

        } else if (event.value == DRV_STANDBY_INTERNAL_TRIGGER) {
            Serial.print(" [standby, internal trigger]");

        } else if (event.value == DRV_INTERNAL_TRIGGER) {
            Serial.print(" [internal trigger, awake]");
        }
    }

    if (event.operation == Operation::WRITE &&
        event.reg == DRV_RTP) {

        Serial.printf(
            " [amplitude=%u",
            event.value
        );

        if (vibration_active) {
            Serial.printf(
                ", vibration +%lu ms",
                static_cast<unsigned long>(
                    event.timestamp_ms -
                    vibration_start_ms
                )
            );
        }

        Serial.print("]");
    }

    Serial.println();
}


/* ============================================================
 * Generic log dispatch
 * ============================================================ */

static void print_event(const LogEvent &event)
{
    if (event.device == DeviceId::NBM) {
        print_nbm(event);
    } else {
        print_drv(event);
    }
}


/* ============================================================
 * Setup
 * ============================================================ */

void setup()
{
    Serial.begin(115200);
    delay(500);

    memset(nbm_registers, 0, sizeof(nbm_registers));
    memset(drv_registers, 0, sizeof(drv_registers));

    /*
     * The firmware probes the DRV2605 by reading STATUS.
     * Any readable value is enough for this simulator.
     */
    drv_registers[DRV_STATUS] = 0xE0;

    log_queue = xQueueCreate(
        64,
        sizeof(LogEvent)
    );

    if (log_queue == nullptr) {
        Serial.println("ERROR: could not create log queue");

        while (true) {
            delay(1000);
        }
    }

    /*
     * NBM slave
     */
    Wire.onReceive(on_nbm_receive);
    Wire.onRequest(on_nbm_request);

    bool nbm_ok = Wire.begin(
        NBM_ADDRESS,
        NBM_SDA,
        NBM_SCL,
        I2C_HZ
    );

    /*
     * DRV slave
     */
    Wire1.onReceive(on_drv_receive);
    Wire1.onRequest(on_drv_request);

    bool drv_ok = Wire1.begin(
        DRV_ADDRESS,
        DRV_SDA,
        DRV_SCL,
        I2C_HZ
    );

#if CONFIG_IDF_TARGET_ESP32

    /*
     * Required by some older classic ESP32 Arduino
     * cores for slave reads.
     */
    uint8_t nbm_status =
        nbm_registers[NBM_STATUS];

    uint8_t drv_status =
        drv_registers[DRV_STATUS];

    Wire.slaveWrite(
        &nbm_status,
        1
    );

    Wire1.slaveWrite(
        &drv_status,
        1
    );

#endif

    Serial.println();
    Serial.println("=== Joya dual haptic I2C simulator ===");

    Serial.printf(
        "NBM 0x%02X GPIO%d/GPIO%d: %s\n",
        NBM_ADDRESS,
        NBM_SDA,
        NBM_SCL,
        nbm_ok ? "READY" : "FAILED"
    );

    Serial.printf(
        "DRV 0x%02X GPIO%d/GPIO%d: %s\n",
        DRV_ADDRESS,
        DRV_SDA,
        DRV_SCL,
        drv_ok ? "READY" : "FAILED"
    );

    Serial.println(
        "Expected NBM init: "
        "07=00 09=7D 0A=34 0B=00 0C=00 08=02"
    );

    Serial.println(
        "Expected DRV init: "
        "STATUS read, RTP_INPUT=00, MODE=00"
    );

    Serial.println("Waiting for the DK...");
    Serial.println();
}


/* ============================================================
 * Loop
 * ============================================================ */

void loop()
{
    LogEvent event;

    while (
        xQueueReceive(
            log_queue,
            &event,
            0
        ) == pdTRUE
    ) {
        print_event(event);
    }

    if (dropped_logs != 0) {
        uint32_t count = dropped_logs;
        dropped_logs = 0;

        Serial.printf(
            "WARNING: %lu log events dropped\n",
            static_cast<unsigned long>(count)
        );
    }

    delay(1);
}