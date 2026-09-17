#pragma once

// ===========================================================================
// ESP8266 pin map for the bench HIL build.
// ===========================================================================
//
// Assumes a NodeMCU-style ESP8266 devkit (bare ESP-01/ESP-12 modules do NOT
// have enough exposed GPIOs for this many peripherals — if that's what you
// have, you need a devkit with more breakout pins, e.g. a Wemos D1 mini or
// NodeMCU v2/v3, or move to an ESP32).
//
// A bare ESP8266 is genuinely tight with six modules on it. This map fits
// five (IMU, barometer, LoRa, servo, GNSS) plus the battery ADC. DHT11 is
// deliberately left UNWIRED for this round: it's bonus ambient telemetry,
// not part of the flight-safety logic, and there simply isn't a free GPIO
// left once everything flight-relevant is connected. Hardware::readAmbientConditions()
// already returns false gracefully when it's not wired (same pattern the
// original desktop HAL uses for "SD card not present"). If you want DHT11
// too, either free up a pin by dropping something else, add an I2C GPIO
// expander (PCF8574), or move to an ESP32 next revision.
//
// VERIFY BEFORE WIRING — this was written without access to your actual
// board, so double check GPIO numbers against your specific devkit's
// silkscreen before connecting anything.
//
// --- I2C bus (shared: MPU6050 @ 0x68, BMP280 @ 0x76 or 0x77) ---
#define HIL_PIN_I2C_SDA   D2   // GPIO4
#define HIL_PIN_I2C_SCL   D1   // GPIO5

// --- LoRa 433MHz (SX1278/RA-02 style module, via hardware SPI) ---
#define HIL_PIN_LORA_SCK  D5   // GPIO14 (HSPI SCK)
#define HIL_PIN_LORA_MISO D6   // GPIO12 (HSPI MISO)
#define HIL_PIN_LORA_MOSI D7   // GPIO13 (HSPI MOSI)
#define HIL_PIN_LORA_NSS  D8   // GPIO15 — needs an external 10k pull-down to
                               // GND (standard NodeMCU D8/GPIO15 boot
                               // requirement) so the board doesn't fail to
                               // boot if the LoRa module's CS line floats high.
#define HIL_PIN_LORA_RST  D0   // GPIO16
#define HIL_PIN_LORA_DIO0 D4   // GPIO2 — shared with the onboard LED.
                               // In the polling mode this firmware uses
                               // (Hardware::pollRadio() calls LoRa.parsePacket()
                               // every tick, no attachInterrupt), DIO0 does not
                               // strictly need to be physically wired. Leave
                               // it disconnected if you're out of pins; connect
                               // it if you later switch to interrupt-driven RX.

// --- GPS NEO-6M (SoftwareSerial, RX only — we never send commands to it) ---
#define HIL_PIN_GPS_RX    D3   // GPIO0 — connect to the GPS module's TX pin.
                               // GPIO0 is a boot-strapping pin (must not be
                               // pulled LOW while the ESP8266 is resetting).
                               // A GPS module's idle TX line is normally
                               // high/idle and shouldn't interfere, but if you
                               // get random boot failures, this is the first
                               // pin to suspect.

// --- Servo (payload separation actuator) ---
#define HIL_PIN_SERVO     3    // GPIO3 (the hardware Serial RX pin's GPIO
                               // number), reused as a plain digital output.
                               // Written as a literal rather than the `RX`
                               // macro because not every ESP8266 core
                               // variant defines that convenience macro —
                               // GPIO3 is the same physical pin either way.
                               // This firmware never calls Serial.read()
                               // (the test scenario is chosen at compile
                               // time — see config.hpp's HIL_SCENARIO — not
                               // typed over the serial monitor), so GPIO3 is
                               // free for the servo. Serial.print() debug
                               // output still works fine over TX alone.

// --- Battery voltage (through your own resistor divider into A0) ---
// A0 on a bare ESP8266 reads 0-1.0V; many NodeMCU devkits add an onboard
// divider bringing that range to roughly 0-3.3V. VERIFY WHICH YOUR BOARD HAS
// before trusting readBatteryVoltage() — this file has no way to tell them
// apart, and HIL_BATTERY_DIVIDER_RATIO below is a guess until you calibrate
// it against a multimeter reading of your actual pack.
#define HIL_PIN_BATTERY_ADC A0
