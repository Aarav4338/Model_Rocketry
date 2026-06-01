#pragma once

#include <cstdint>

// The Hardware Abstraction Layer (HAL) bridges the gap between the platform-independent
// mission state machine and the actual physical hardware. 
// When the MCU, sensors, and radio modules are selected, their specific driver code
// (e.g. STM32 HAL calls, SPI transfers) will be implemented here.

namespace Hardware
{
    // --- System Initialization ---
    bool initMCU();
    bool initWatchdog(uint32_t timeout_ms);
    void resetWatchdog();
    void systemReset();

    // --- Communication Buses ---
    bool initI2C();
    bool initSPI();
    bool initUART();
    bool initCAN();

    // --- Sensors ---
    bool initPrimaryIMU();
    bool initRedundantAltimeter();
    bool initGNSS();

    // Sensor Polling (To be populated with actual sensor structures/structs)
    bool readIMU(float &accel_x, float &accel_y, float &accel_z, float &gyro_x, float &gyro_y, float &gyro_z);
    bool readBarometer(float &pressure_pa, float &temperature_c);
    bool readRedundantBarometer(float &pressure_pa, float &temperature_c);
    bool readGNSS(uint32_t &time, double &lat, double &lon, float &alt, uint8_t &sats);
    float readBatteryVoltage();

    // --- Telemetry & Radio ---
    bool initRadio();
    bool transmitTelemetry(const char* packet, uint16_t length);
    bool transmitCANPayload(const char* packet, uint16_t length);

    // --- Storage ---
    bool initSDCard();
    bool writeToSDCard(const char* data, uint16_t length);
    void flushSDCard();

    // --- Actuation & Power ---
    void triggerDeploymentCharge();
    void setRecoveryBeacon(bool active);
    void setPowerDownMode();
}
