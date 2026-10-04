#include <Arduino.h>
#include <Wire.h>

// Classic ESP32 with two I2C controllers, connected to one DK I2C bus:
// DK SDA -> GPIO21 and GPIO25; DK SCL -> GPIO22 and GPIO26.
// Share GND, add SDA/SCL pull-ups to 3.3 V, and hold DK NBM RDY HIGH.
static constexpr uint8_t NBM_ADDR = 0x2E;
static constexpr uint8_t DRV_ADDR = 0x5A;
static uint8_t nbmReply = 0x00;
static uint8_t drvReply = 0x60;

void nbmReceive(int count) {
  (void)count;
  while (Wire.available()) Wire.read();
}

void drvReceive(int count) {
  (void)count;
  while (Wire1.available()) Wire1.read();
}

void nbmRequest() { Wire.write(nbmReply); }
void drvRequest() { Wire1.write(drvReply); }

void setup() {
  Wire.onReceive(nbmReceive);
  Wire.onRequest(nbmRequest);
  Wire.begin(NBM_ADDR, 21, 22, 100000);

  Wire1.onReceive(drvReceive);
  Wire1.onRequest(drvRequest);
  Wire1.begin(DRV_ADDR, 25, 26, 100000);

#if CONFIG_IDF_TARGET_ESP32
  // Some older classic ESP32 Arduino cores need an initial slave reply.
  Wire.slaveWrite(&nbmReply, 1);
  Wire1.slaveWrite(&drvReply, 1);
#endif
}

void loop() {}
