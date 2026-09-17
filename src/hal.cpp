#include "hal.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cctype>

namespace Hardware
{
    // --- System Initialization ---
    bool initMCU() { return true; }
    
    bool initWatchdog(uint32_t timeout_ms) { 
        (void)timeout_ms;
        return true; 
    }
    
    void resetWatchdog() {}
    
    void systemReset() {
        std::cout << "[HARDWARE] System Reset Triggered\n";
    }

    // --- Communication Buses ---
    bool initI2C() { return true; }
    bool initSPI() { return true; }
    bool initUART() { return true; }
    bool initCAN() { return true; }

    // --- Sensors ---
    bool initPrimaryIMU() {
        // TODO: STM32 HAL_SPI_Init() for Primary IMU
        return true;
    }

    bool initRedundantAltimeter() {
        // TODO: STM32 HAL_I2C_Init() for Redundant Altimeter
        return true;
    }

    bool initGNSS() {
        return true;
    }

    bool readIMU(float &accel_x, float &accel_y, float &accel_z, float &gyro_x, float &gyro_y, float &gyro_z) {
        // Simulated zero readings
        accel_x = accel_y = accel_z = 0.0f;
        gyro_x = gyro_y = gyro_z = 0.0f;
        return true;
    }

    bool readBarometer(float &pressure_pa, float &temperature_c) {
        pressure_pa = 101325.0f;
        temperature_c = 25.0f;
        return true;
    }

    bool readRedundantBarometer(float &pressure_pa, float &temperature_c) {
        pressure_pa = 101325.0f;
        temperature_c = 25.0f;
        return true;
    }

    bool readGNSS(uint32_t &time, float &lat, float &lon, float &alt, uint8_t &sats) {
        time = 0;
        lat = 0.0f;
        lon = 0.0f;
        alt = 0.0f;
        sats = 8;
        return true;
    }

    float readBatteryVoltage() {
        return 7.4f; // Simulated 2S LiPo
    }

    // --- Ambient Environment (DHT11) ---
    bool initAmbientSensor() {
        // No DHT11 on the desktop build.
        return false;
    }

    bool readAmbientConditions(float &humidity_pct, float &ambient_temp_c) {
        humidity_pct = 45.0f;
        ambient_temp_c = 25.0f;
        return false;
    }

    // --- Telemetry & Radio ---
    bool initRadio() {
        // TODO: STM32 HAL_UART_Init() for LoRa / XBEE
        return true;
    }

    bool transmitTelemetry(const char* packet, uint16_t length) {
        if (packet && length > 0) {
            // Desktop: also echo radio transmissions to console for observability
            std::cout << "[RADIO TX] " << std::string(packet, packet + length) << std::endl;
        }
        return true;
    }

    bool transmitCANPayload(const char* packet, uint16_t length) {
        (void)packet;
        (void)length;
        // TODO: STM32 HAL_CAN_AddTxMessage()
        return true;
    }

    bool pollRadio(char* outBuf, uint16_t maxLen) {
        if (outBuf == nullptr || maxLen == 0) return false;

        // Desktop helper: read first non-empty line from telecommands.txt
        const char *fname = "telecommands.txt";
        std::ifstream in(fname);
        if (!in) return false;

        std::string line;
        std::vector<std::string> rest;
        bool got = false;
        while (std::getline(in, line)) {
            if (!got && !line.empty()) {
                // take this as the incoming packet
                got = true;
            } else {
                rest.push_back(line);
            }
            if (got) break; // consume single packet only
        }
        in.close();

        if (!got) return false;

        // write remaining lines back (if any)
        std::ofstream out(fname, std::ios::trunc);
        for (const auto &l : rest) out << l << "\n";
        out.close();

        // Trim whitespace from both ends
        auto trim = [](std::string &s) {
            size_t a = 0;
            while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            size_t b = s.size();
            while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
            s = s.substr(a, b - a);
        };

        trim(line);
        // copy to outBuf safely
        size_t copy_len = (line.size() < (size_t)maxLen - 1) ? line.size() : (size_t)maxLen - 1;
        std::memcpy(outBuf, line.data(), copy_len);
        outBuf[copy_len] = '\0';
        return true;
    }

    bool radioIsReady() {
        // Desktop stub always reports ready if radio init succeeded.
        return true;
    }

    // --- Storage ---
    static std::ofstream sd_card_file;

    bool initSDCard() {
        // Simulating STM32 SDIO / SPI initialization with a local file on desktop
        // Open in append mode, create if it doesn't exist.
        sd_card_file.open("sd_card_log.txt", std::ios::app);
        if(!sd_card_file) {
            std::cerr << "[HARDWARE] Failed to initialize SD Card (could not open file).\n";
            return false;
        }
        std::cout << "[HARDWARE] SD Card Initialized.\n";
        return true;
    }

    bool writeToSDCard(const char* data, uint16_t length) {
        if(sd_card_file.is_open()) {
            sd_card_file.write(data, length);
            return true;
        }
        return false;
    }

    void flushSDCard() {
        if(sd_card_file.is_open()) {
            sd_card_file.flush();
        }
    }

    // --- Actuation & Power ---
    void triggerDeploymentCharge() {
        std::cout << "[HARDWARE] Deployment Charge Fired!\n";
    }

    void setRecoveryBeacon(bool active) {
        if(active) {
            std::cout << "[HARDWARE] Recovery Beacon Activated.\n";
        }
    }

    void setPowerDownMode() {
        std::cout << "[HARDWARE] Peripherals Powered Down for Recovery.\n";
    }
}
