// ESP8266 Hardware Abstraction Layer implementation.
//
// This is the real-hardware twin of the desktop src/hal.cpp stub. Every
// function here implements the SAME `Hardware::` interface declared in
// include/hal.hpp, so none of the FSM/filter/fault/telemetry logic needs to
// know or care which build it's running in.
//
// In FlightConfig::HIL_MODE (config.hpp), the sensor-reading functions
// (readBarometer/readIMU/readGNSS/readBatteryVoltage) return values derived
// from the injected flight physics (see include/hil_state.hpp) instead of
// the real, stationary sensors -- while radio TX/RX, the servo actuator,
// the EEPROM crash-recovery log, and the watchdog all operate for real. That
// is the entire point of this build: exercise real timing, real buses, and
// real actuation against a fabricated flight, without anything needing to
// physically leave the workbench. Flip HIL_MODE to false once you want the
// real sensors driving the FSM (src/real_sensors.cpp) for an actual flight
// or a physically-excited bench test.

#include "hal.hpp"

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <Servo.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

#include <cmath>
#include <cstring>

#include "config.hpp"
#include "mission_io.hpp"
#include "hil_pins.hpp"
#include "hil_state.hpp"

HilBridge g_hil;

namespace
{
    // ---- MPU6050 (raw I2C register access -- see comments below) ----
    constexpr uint8_t MPU6050_ADDR = 0x68;
    constexpr uint8_t MPU6050_REG_PWR_MGMT_1 = 0x6B;
    constexpr uint8_t MPU6050_REG_ACCEL_XOUT_H = 0x3B;
    // Default full-scale ranges (±2g / ±250 deg/s) -> datasheet sensitivities.
    constexpr float MPU6050_ACCEL_LSB_PER_G = 16384.0f;
    constexpr float MPU6050_GYRO_LSB_PER_DPS = 131.0f;
    constexpr float DEG_TO_RAD_FLOAT = 0.017453293f;

    bool g_imu_ready = false;
    bool g_baro_ready = false;
    bool g_radio_ready = false;
    bool g_sd_ready = false; // "SD card" == LittleFS flight log file, see below

    Adafruit_BMP280 g_bmp;
    SoftwareSerial g_gps_serial(HIL_PIN_GPS_RX, -1); // RX only, no TX wired
    TinyGPSPlus g_gps;
    Servo g_deployment_servo;

    // Deployment servo angles -- adjust to match your actual release
    // mechanism (a linkage that pulls a pin, a burn-wire trigger arm, etc).
    constexpr int SERVO_ANGLE_SAFE = 0;
    constexpr int SERVO_ANGLE_FIRE = 90;

    // "SD card" log file on the ESP8266's onboard flash via LittleFS. There
    // is no SD card in this parts list (see hil_pins.hpp) -- this is the
    // closest equivalent available today. Swap this for a real SPI SD driver
    // once a card module is added; the Hardware:: interface won't need to
    // change, only this file.
    File g_flight_log_file;

    void mpu6050_write_register(uint8_t reg, uint8_t value)
    {
        Wire.beginTransmission(MPU6050_ADDR);
        Wire.write(reg);
        Wire.write(value);
        Wire.endTransmission(true);
    }

    bool mpu6050_read_raw(int16_t &ax, int16_t &ay, int16_t &az,
                          int16_t &gx, int16_t &gy, int16_t &gz)
    {
        Wire.beginTransmission(MPU6050_ADDR);
        Wire.write(MPU6050_REG_ACCEL_XOUT_H);
        if (Wire.endTransmission(false) != 0) return false;

        const uint8_t bytes_expected = 14; // accel(6) + temp(2) + gyro(6)
        if (Wire.requestFrom(static_cast<int>(MPU6050_ADDR),
                              static_cast<int>(bytes_expected),
                              1) != bytes_expected)
        {
            return false;
        }

        auto read16 = []() -> int16_t {
            uint8_t hi = Wire.read();
            uint8_t lo = Wire.read();
            return static_cast<int16_t>((hi << 8) | lo);
        };

        ax = read16();
        ay = read16();
        az = read16();
        read16(); // discard onboard temperature register
        gx = read16();
        gy = read16();
        gz = read16();
        return true;
    }
}

namespace Hardware
{
    // --- System Initialization ---
    bool initMCU()
    {
        Serial.begin(115200);
        delay(200);
        return true;
    }

    bool initWatchdog(uint32_t timeout_ms)
    {
        // The ESP8266 core already runs its own hardware watchdog that
        // resets the chip if the loop() task blocks for too long without
        // yielding. What we add here is ESP.wdtEnable-style software
        // watchdog behavior via resetWatchdog()'s companion ESP.wdtFeed()
        // calls, so a hang in mission logic (not just a blocked loop()) is
        // caught the same way it would be checked in checkWatchdog()'s FSM
        // logic. timeout_ms is accepted for interface parity with hal.hpp;
        // the ESP8266 core's own watchdog timeout is not user-configurable
        // the way an STM32 IWDG prescaler would be.
        (void)timeout_ms;
        return true;
    }

    void resetWatchdog()
    {
        ESP.wdtFeed();
        yield(); // let the ESP8266 SDK service WiFi/SDK housekeeping
    }

    void systemReset()
    {
        MISSION_COUT << "[HARDWARE] System Reset Triggered\n";
        Serial.flush();
        ESP.restart();
    }

    // --- Communication Buses ---
    bool initI2C()
    {
        Wire.begin(HIL_PIN_I2C_SDA, HIL_PIN_I2C_SCL);
        return true;
    }

    bool initSPI()
    {
        SPI.begin();
        return true;
    }

    bool initUART()
    {
        g_gps_serial.begin(9600); // NEO-6M default baud
        return true;
    }

    bool initCAN()
    {
        // The ESP8266 has no CAN peripheral and there's no CAN transceiver
        // in the current parts list. The CAN-7U payload interface mentioned
        // in config.hpp's competition notes would need an external CAN
        // transceiver (e.g. MCP2515 + TJA1050) and its own driver here.
        return false;
    }

    // --- Sensors ---
    bool initPrimaryIMU()
    {
        mpu6050_write_register(MPU6050_REG_PWR_MGMT_1, 0x00); // wake from sleep
        delay(50);
        int16_t ax, ay, az, gx, gy, gz;
        g_imu_ready = mpu6050_read_raw(ax, ay, az, gx, gy, gz);
        return g_imu_ready;
    }

    bool initRedundantAltimeter()
    {
        // Only one BMP280 in the current parts list, so there is no true
        // hardware redundancy yet -- readRedundantBarometer() below reads
        // the same physical sensor a second time. Add a second BMP280 (or a
        // different part, e.g. a BMP390) on a separate I2C address or bus to
        // get genuine sensor redundancy per the competition guidelines.
        g_baro_ready = g_bmp.begin(0x76) || g_bmp.begin(0x77);
        return g_baro_ready;
    }

    bool initGNSS()
    {
        return true; // SoftwareSerial already started in initUART()
    }

    bool readIMU(float &accel_x, float &accel_y, float &accel_z,
                float &gyro_x, float &gyro_y, float &gyro_z)
    {
        if (FlightConfig::HIL_MODE)
        {
            // Fabricate a plausible 3-axis reading consistent with the
            // injected flight's scalar accel magnitude: mostly on Z (the
            // rocket's long axis on the pad), matching how
            // simulateIMUAndPower() in simulation.cpp only ever produces a
            // magnitude, not a 3-axis vector.
            accel_x = 0.0f;
            accel_y = 0.0f;
            accel_z = g_hil.accel_magnitude;
            gyro_x = gyro_y = gyro_z = 0.0f;
            return true;
        }

        if (!g_imu_ready) return false;

        int16_t ax, ay, az, gx, gy, gz;
        if (!mpu6050_read_raw(ax, ay, az, gx, gy, gz)) return false;

        accel_x = (ax / MPU6050_ACCEL_LSB_PER_G) * FlightConfig::SIM_GRAVITY_MPS2;
        accel_y = (ay / MPU6050_ACCEL_LSB_PER_G) * FlightConfig::SIM_GRAVITY_MPS2;
        accel_z = (az / MPU6050_ACCEL_LSB_PER_G) * FlightConfig::SIM_GRAVITY_MPS2;

        gyro_x = (gx / MPU6050_GYRO_LSB_PER_DPS) * DEG_TO_RAD_FLOAT;
        gyro_y = (gy / MPU6050_GYRO_LSB_PER_DPS) * DEG_TO_RAD_FLOAT;
        gyro_z = (gz / MPU6050_GYRO_LSB_PER_DPS) * DEG_TO_RAD_FLOAT;
        return true;
    }

    bool readBarometer(float &pressure_pa, float &temperature_c)
    {
        if (FlightConfig::HIL_MODE)
        {
            // Standard barometric formula, inverted: derive the pressure
            // that WOULD produce g_hil.true_altitude, so telemetry's
            // pressure/temperature columns stay numerically consistent with
            // the fake flight the FSM is reacting to.
            pressure_pa = 101325.0f *
                std::pow(1.0f - (2.25577e-5f * g_hil.true_altitude), 5.25588f);
            temperature_c = 25.0f - (0.0065f * g_hil.true_altitude);
            return true;
        }

        if (!g_baro_ready) return false;
        pressure_pa = g_bmp.readPressure();
        temperature_c = g_bmp.readTemperature();
        return true;
    }

    bool readRedundantBarometer(float &pressure_pa, float &temperature_c)
    {
        // See the comment in initRedundantAltimeter(): same physical sensor
        // until a second barometer is added.
        return readBarometer(pressure_pa, temperature_c);
    }

    bool readGNSS(uint32_t &time, float &lat, float &lon, float &alt, uint8_t &sats)
    {
        // Drain whatever NMEA bytes have arrived since the last call.
        while (g_gps_serial.available() > 0)
        {
            g_gps.encode(g_gps_serial.read());
        }

        if (FlightConfig::HIL_MODE)
        {
            // Report zero satellites so sensors.cpp's existing "no GNSS
            // fix" fallback branch synthesizes drifting coordinates tied to
            // the filtered altitude -- see updateSensors() in sensors.cpp.
            time = 0;
            lat = 0.0f;
            lon = 0.0f;
            alt = 0.0f;
            sats = 0;
            return true;
        }

        sats = g_gps.satellites.isValid() ?
            static_cast<uint8_t>(g_gps.satellites.value()) : 0;

        if (g_gps.location.isValid())
        {
            lat = static_cast<float>(g_gps.location.lat());
            lon = static_cast<float>(g_gps.location.lng());
        }
        else
        {
            lat = 0.0f;
            lon = 0.0f;
        }

        alt = g_gps.altitude.isValid() ?
            static_cast<float>(g_gps.altitude.meters()) : 0.0f;

        time = g_gps.time.isValid() ?
            static_cast<uint32_t>(g_gps.time.value()) : 0;

        return true;
    }

    float readBatteryVoltage()
    {
        if (FlightConfig::HIL_MODE)
        {
            return g_hil.battery_voltage;
        }

        // analogRead() on ESP8266 returns 0-1023 for 0-1.0V at the ADC pin
        // itself. FlightConfig::HIL_BATTERY_DIVIDER_RATIO converts that back
        // up to the real pack voltage through your external divider --
        // CALIBRATE THIS against a multimeter before trusting it (see the
        // caveat in hil_pins.hpp about which ADC range your specific board
        // exposes on A0).
        const float adc_volts = (analogRead(HIL_PIN_BATTERY_ADC) / 1023.0f) * 1.0f;
        return adc_volts * FlightConfig::HIL_BATTERY_DIVIDER_RATIO;
    }

    // --- Ambient Environment (DHT11) ---
    bool initAmbientSensor()
    {
        // Not wired this round -- see hil_pins.hpp for why. Returns false,
        // same as the desktop stub, so callers already treat this as
        // optional/degraded rather than a hard failure.
        return false;
    }

    bool readAmbientConditions(float &humidity_pct, float &ambient_temp_c)
    {
        humidity_pct = 0.0f;
        ambient_temp_c = 0.0f;
        return false;
    }

    // --- Telemetry & Radio ---
    bool initRadio()
    {
        LoRa.setPins(HIL_PIN_LORA_NSS, HIL_PIN_LORA_RST, HIL_PIN_LORA_DIO0);
        g_radio_ready = LoRa.begin(433E6);
        return g_radio_ready;
    }

    bool transmitTelemetry(const char *packet, uint16_t length)
    {
        if (!g_radio_ready || packet == nullptr || length == 0) return false;

        LoRa.beginPacket();
        LoRa.write(reinterpret_cast<const uint8_t *>(packet), length);
        LoRa.endPacket();

        // Mirror to USB serial too, same observability the desktop build
        // gets from its "[RADIO TX]" console echo.
        MISSION_COUT << "[RADIO TX] " << packet << MISSION_ENDL;
        return true;
    }

    bool transmitCANPayload(const char *packet, uint16_t length)
    {
        (void)packet;
        (void)length;
        // No CAN transceiver in the current parts list -- see initCAN().
        return false;
    }

    bool pollRadio(char *outBuf, uint16_t maxLen)
    {
        if (outBuf == nullptr || maxLen == 0) return false;

        int packet_size = LoRa.parsePacket();
        if (packet_size <= 0) return false;

        uint16_t copy_len = 0;
        while (LoRa.available() && copy_len < static_cast<uint16_t>(maxLen - 1))
        {
            outBuf[copy_len++] = static_cast<char>(LoRa.read());
        }
        outBuf[copy_len] = '\0';
        return copy_len > 0;
    }

    bool radioIsReady()
    {
        return g_radio_ready;
    }

    // --- Storage ---
    bool initSDCard()
    {
        // See the comment above g_flight_log_file: this is LittleFS on the
        // ESP8266's onboard flash, not a real SD card -- there isn't one in
        // the current parts list. Swap this for a real SD driver later
        // without touching any caller of Hardware::writeToSDCard().
        if (!LittleFS.begin())
        {
            return false;
        }
        g_flight_log_file = LittleFS.open("/sd_card_log.txt", "a");
        g_sd_ready = static_cast<bool>(g_flight_log_file);
        return g_sd_ready;
    }

    bool writeToSDCard(const char *data, uint16_t length)
    {
        if (!g_sd_ready || !g_flight_log_file) return false;
        return g_flight_log_file.write(reinterpret_cast<const uint8_t *>(data), length) == length;
    }

    void flushSDCard()
    {
        if (g_sd_ready && g_flight_log_file)
        {
            g_flight_log_file.flush();
        }
    }

    // --- Actuation & Power ---
    void triggerDeploymentCharge()
    {
        // Servo-actuated release rather than a pyro charge, per the current
        // bench setup (a single test servo). Swap this for real e-match
        // firing logic once you're driving an actual pyro channel.
        g_deployment_servo.write(SERVO_ANGLE_FIRE);
        MISSION_COUT << "[HARDWARE] Deployment servo fired!\n";
    }

    void setRecoveryBeacon(bool active)
    {
        // No dedicated beacon LED/buzzer wired yet -- reuse the onboard LED
        // (shared with LoRa DIO0, see hil_pins.hpp) as a visible indicator.
        digitalWrite(LED_BUILTIN, active ? LOW : HIGH); // LED_BUILTIN is active-low on most ESP8266 boards
        if (active)
        {
            MISSION_COUT << "[HARDWARE] Recovery Beacon Activated.\n";
        }
    }

    void setPowerDownMode()
    {
        MISSION_COUT << "[HARDWARE] Peripherals Powered Down for Recovery.\n";
        LoRa.sleep();
    }
}

// Called once from setup() in main.cpp, after Hardware::init*() calls, to
// attach the deployment servo. Kept out of the Hardware:: namespace since
// hal.hpp has no initServo() entry point (the desktop build has no actuator
// hardware at all to initialize).
void hilAttachDeploymentServo()
{
    g_deployment_servo.attach(HIL_PIN_SERVO);
    g_deployment_servo.write(SERVO_ANGLE_SAFE);
}
