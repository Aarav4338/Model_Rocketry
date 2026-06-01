#include "telemetry.hpp"

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstring>

#include "config.hpp"
#include "sensors.hpp"

// ---------------------------------------------------------------------------
// State / Phase name helpers (used in CSV and console output)
// ---------------------------------------------------------------------------

static const char *stateName(State state)
{
    switch(state)
    {
        case BOOT:               return "BOOT";
        case TEST_MODE:          return "TEST_MODE";
        case Prelaunch_Check:    return "Prelaunch_Check";
        case Launch_Pad:         return "Launch_Pad";
        case Ascent:             return "Ascent";
        case Apogee_confirm:     return "Apogee_confirm";
        case Payload_Separation: return "Payload_Separation";
        case Descent:            return "Descent";
        case Landed:             return "Landed";
        case Beacon:             return "Beacon";
        default:                 return "UNKNOWN";
    }
}

static const char *flightPhaseName(FlightPhase phase)
{
    switch(phase)
    {
        case Flight_Grounded:          return "Grounded";
        case Flight_Powered_Ascent:    return "Powered ascent";
        case Flight_Coast:             return "Coast";
        case Flight_Ballistic_Descent: return "Ballistic descent";
        case Flight_Landed:            return "Landed";
        default:                       return "Unknown";
    }
}

// ===========================================================================
// SX1262 / E22-900M22S  —  STM32 HAL SPI Driver
// ===========================================================================
//
// The E22-900M22S is an "M-series" Ebyte module: it directly exposes the
// Semtech SX1262 SPI bus to the host MCU.  There is NO onboard UART bridge
// (that would be the "T-series").  Every command must be sent as a raw SPI
// transaction following the SX1262 datasheet protocol.
//
// ── Physical wiring (STM32 ↔ E22-900M22S) ──────────────────────────────────
//   STM32 SPI1_MOSI  →  Module MOSI
//   STM32 SPI1_MISO  ←  Module MISO
//   STM32 SPI1_SCK   →  Module SCK
//   STM32 GPIO_OUT   →  Module NSS   (chip-select, active-LOW)
//   STM32 GPIO_IN    ←  Module BUSY  (HIGH = chip busy, wait before sending)
//   STM32 GPIO_OUT   →  Module NRESET (pull LOW to reset, then release)
//
// !! IMPORTANT: E22-900M22S is a 3.3 V device.  Do NOT connect 5 V logic. !!
//
// ── STM32CubeIDE setup steps ────────────────────────────────────────────────
//   1. In CubeMX enable SPI1 in "Full-Duplex Master" mode.
//      Set Prescaler so that BAUD RATE ≤ 16 MHz (SX1262 max).
//      Data Size = 8 bit, CPOL = LOW, CPHA = 1 Edge (Mode 0).
//   2. Configure three additional GPIOs:
//        PA4  → Output, PP, No Pull  (NSS)
//        PA5  → Input,  No Pull      (BUSY)
//        PA6  → Output, PP, Pull-Up  (NRESET)
//   3. Generate code; include "spi.h" and "gpio.h" in this file.
//   4. Call sx1262_init() once after HAL_Init() and before the mission loop.
//   5. The STM32 section at the bottom of each function is ready to uncomment.
// ===========================================================================

// ── SX1262 OpCode constants (datasheet Table 11-1) ──────────────────────────
// These are the raw command bytes sent as the first byte of every SPI frame.
static const uint8_t SX1262_CMD_SET_STANDBY         = 0x80;
static const uint8_t SX1262_CMD_SET_PACKET_TYPE     = 0x8A;
static const uint8_t SX1262_CMD_SET_RF_FREQUENCY    = 0x86;
static const uint8_t SX1262_CMD_SET_TX_PARAMS       = 0x8E;
static const uint8_t SX1262_CMD_SET_MOD_PARAMS      = 0x8B;
static const uint8_t SX1262_CMD_SET_PACKET_PARAMS   = 0x8C;
static const uint8_t SX1262_CMD_WRITE_BUFFER        = 0x0E;
static const uint8_t SX1262_CMD_SET_TX              = 0x83;
static const uint8_t SX1262_CMD_READ_BUFFER         = 0x1E;
static const uint8_t SX1262_CMD_GET_RX_BUFFER_STATUS= 0x13;

// Packet type: 0x00 = GFSK, 0x01 = LoRa
static const uint8_t SX1262_PACKET_TYPE_LORA        = 0x01;

// ── Low-level SPI helpers ────────────────────────────────────────────────────
// On desktop these are completely empty (no hardware).  On STM32 each one maps
// to exactly one HAL call.

// Pull NSS LOW to begin a transaction
static inline void sx1262_nss_low()
{
    // STM32: HAL_GPIO_WritePin(NSS_GPIO_Port, NSS_Pin, GPIO_PIN_RESET);
}

// Pull NSS HIGH to end a transaction
static inline void sx1262_nss_high()
{
    // STM32: HAL_GPIO_WritePin(NSS_GPIO_Port, NSS_Pin, GPIO_PIN_SET);
}

// Block until the BUSY pin goes LOW (chip is ready to accept a command).
// The SX1262 asserts BUSY HIGH for up to ~3.5 ms after a command; we must
// not start the next NSS transaction until BUSY is LOW again.
static inline void sx1262_wait_on_busy()
{
    // STM32:
    // while (HAL_GPIO_ReadPin(BUSY_GPIO_Port, BUSY_Pin) == GPIO_PIN_SET) {}
}

// Send one byte and simultaneously receive one byte over SPI
static inline uint8_t sx1262_spi_transfer(uint8_t tx_byte)
{
    (void)tx_byte;
    // STM32:
    // uint8_t rx_byte = 0;
    // HAL_SPI_TransmitReceive(&hspi1, &tx_byte, &rx_byte, 1, HAL_MAX_DELAY);
    // return rx_byte;
    return 0x00; // desktop: return dummy NOP byte
}

// Send a complete command: opcode + optional data bytes.
// Automatically handles the NSS and BUSY handshake around every transaction.
static void sx1262_write_command(uint8_t opcode,
                                 const uint8_t* data,
                                 uint8_t data_len)
{
    sx1262_wait_on_busy();
    sx1262_nss_low();

    sx1262_spi_transfer(opcode);           // send opcode first
    for (uint8_t i = 0; i < data_len; i++)
    {
        sx1262_spi_transfer(data[i]);      // then send each parameter byte
    }

    sx1262_nss_high();
    // BUSY will go HIGH briefly while the chip processes; next call to
    // sx1262_wait_on_busy() will block until it settles LOW again.
}

// ── One-time radio initialisation ────────────────────────────────────────────
// Call this once at startup (e.g., inside runBootState) before the mission
// loop begins.  Configures the SX1262 for 915 MHz LoRa transmission with
// settings matched to the competition environment.
//
// Parameters chosen:
//   Frequency  : 915.0 MHz  (adjust to your regional band — 868 for EU)
//   SF         : 9          (good range/speed balance at ~1 km altitude)
//   BW         : 125 kHz
//   CR         : 4/5
//   TX Power   : +22 dBm    (E22-900M22S maximum — check local regulations)
void sx1262_init()
{
    // 1. Enter STDBY_RC so all subsequent commands are accepted
    {
        uint8_t args[] = {0x00};           // 0x00 = STDBY_RC, 0x01 = STDBY_XOSC
        sx1262_write_command(SX1262_CMD_SET_STANDBY, args, 1);
    }

    // 2. Select LoRa packet type
    {
        uint8_t args[] = {SX1262_PACKET_TYPE_LORA};
        sx1262_write_command(SX1262_CMD_SET_PACKET_TYPE, args, 1);
    }

    // 3. Set RF frequency to 915.0 MHz
    //    Formula: rfFreq = (frequency / 32e6) * 2^25  →  915e6/32e6 * 33554432
    //    = 958,857,216 = 0x391D7000
    {
        uint8_t args[] = {0x39, 0x1D, 0x70, 0x00};
        sx1262_write_command(SX1262_CMD_SET_RF_FREQUENCY, args, 4);
    }

    // 4. Set TX power = +22 dBm, ramp time = 200 µs (0x04)
    {
        uint8_t args[] = {0x16, 0x04};     // 0x16 = 22 decimal
        sx1262_write_command(SX1262_CMD_SET_TX_PARAMS, args, 2);
    }

    // 5. Set LoRa modulation params: SF9, BW 125 kHz, CR 4/5, LDRO auto
    //    SF=9 → 0x09 | BW 125k → 0x04 | CR 4/5 → 0x01 | LDRO off → 0x00
    {
        uint8_t args[] = {0x09, 0x04, 0x01, 0x00};
        sx1262_write_command(SX1262_CMD_SET_MOD_PARAMS, args, 4);
    }

    // 6. Set packet params (will be overridden per-packet with actual payload length)
    //    Preamble=12, Explicit header, PayloadLen=0 (updated at TX time),
    //    CRC ON, Standard IQ
    {
        uint8_t args[] = {0x00, 0x0C,   // preamble length MSB/LSB = 12
                          0x00,          // explicit header
                          0x00,          // payload length (placeholder)
                          0x01,          // CRC ON
                          0x00};         // standard IQ
        sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, args, 6);
    }
}

// ── Transmit one ASCII CSV row over LoRa ─────────────────────────────────────
// Called by sendTelemetry() for every packet.  On desktop: no-op.
// On STM32: writes the payload into the SX1262 FIFO, arms the packet length,
// then fires a single LoRa transmission.
static void transmitOverLoRa(const char* csv_line, unsigned int length)
{
    // Clamp to SX1262 maximum payload size (255 bytes)
    if (length > 255) length = 255;

    // ----- DESKTOP BUILD: no hardware attached -----
    (void)csv_line;

    // ----------------------------------------------------------------
    // STM32 EMBEDDED BUILD — uncomment the block below when porting
    // ----------------------------------------------------------------
    //
    // Step A: Update packet params with the real payload length for this packet
    // {
    //     uint8_t args[] = {0x00, 0x0C, 0x00,
    //                       (uint8_t)length,   // <-- actual byte count
    //                       0x01, 0x00};
    //     sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, args, 6);
    // }
    //
    // Step B: Write payload into TX buffer at offset 0x00
    // {
    //     sx1262_wait_on_busy();
    //     sx1262_nss_low();
    //     sx1262_spi_transfer(SX1262_CMD_WRITE_BUFFER);
    //     sx1262_spi_transfer(0x00);                     // buffer offset
    //     for (unsigned int i = 0; i < length; i++)
    //     {
    //         sx1262_spi_transfer((uint8_t)csv_line[i]); // payload bytes
    //     }
    //     sx1262_nss_high();
    // }
    //
    // Step C: Start single LoRa transmission (timeout = 0 → no timeout)
    // {
    //     uint8_t args[] = {0x00, 0x00, 0x00};           // 24-bit timeout = 0
    //     sx1262_write_command(SX1262_CMD_SET_TX, args, 3);
    //     // BUSY will go HIGH during TX and return LOW when packet is sent.
    //     // If you need to know when TX is done, poll BUSY or use DIO1 IRQ.
    //     sx1262_wait_on_busy();
    // }
    // ----------------------------------------------------------------
}

// ===========================================================================
// POINT 4: RF Command Receiver (uplink: Ground Station → Rocket)
// ===========================================================================
//
// The SX1262 can also RECEIVE packets (it is a full transceiver).
// After each TX the rocket briefly switches to RX mode to listen for
// a ground-station uplink command (CMD_ON / CMD_OFF).
//
// Supported commands (raw ASCII, ≤7 bytes):
//   "CMD_OFF" — ground station mutes the telemetry stream.
//   "CMD_ON"  — ground station re-enables telemetry.
//
// On desktop: no hardware, so the function always returns with no action.
// On STM32:  switch to RX for a brief window, read the buffer, act on cmd.
// ---------------------------------------------------------------------------
void receiveRFCommands(RocketSystem &system)
{
    // ----- DESKTOP BUILD: no radio, no commands to receive -----
    // ----- DESKTOP BUILD: no hardware attached -----
    std::memset(system.last_rf_command, 0, sizeof(system.last_rf_command));

    // ----------------------------------------------------------------
    // STM32 EMBEDDED BUILD — uncomment the block below when porting
    //
    // The SX1262 is a TDD (time-division duplex) radio: it can only TX
    // or RX at one time.  We briefly switch to RX mode after each TX
    // to listen for a ground-station uplink command.
    // ----------------------------------------------------------------
    //
    // Step A: Switch to single RX mode with a short timeout (e.g. 100 ms)
    //         Timeout = (100ms / 15.625us) = 6400 = 0x001900
    // {
    //     uint8_t args[] = {0x00, 0x19, 0x00};
    //     sx1262_write_command(0x82, args, 3);  // SetRx opcode = 0x82
    // }
    //
    // Step B: Poll the IRQ status to see if a packet was received
    //         (or use DIO1 as an interrupt pin for efficiency)
    // {
    //     // GetIrqStatus opcode = 0x12, returns 2 IRQ bytes
    //     sx1262_wait_on_busy();
    //     sx1262_nss_low();
    //     sx1262_spi_transfer(0x12);
    //     sx1262_spi_transfer(0x00);            // NOP
    //     uint8_t irq_msb = sx1262_spi_transfer(0x00);
    //     uint8_t irq_lsb = sx1262_spi_transfer(0x00);
    //     sx1262_nss_high();
    //
    //     bool rx_done = (irq_lsb & 0x02) != 0;  // bit 1 = RxDone
    //     bool crc_err = (irq_lsb & 0x40) != 0;  // bit 6 = CrcError
    //     (void)irq_msb;
    //
    //     if (rx_done && !crc_err)
    //     {
    //         // Step C: Find out how many bytes arrived and where in buffer
    //         sx1262_wait_on_busy();
    //         sx1262_nss_low();
    //         sx1262_spi_transfer(SX1262_CMD_GET_RX_BUFFER_STATUS);
    //         sx1262_spi_transfer(0x00);           // NOP status byte
    //         uint8_t payload_len    = sx1262_spi_transfer(0x00);
    //         uint8_t buffer_offset  = sx1262_spi_transfer(0x00);
    //         sx1262_nss_high();
    //
    //         // Step D: Read the payload from the RX buffer
    //         uint8_t rx_buf[8] = {0};
    //         uint8_t read_len = payload_len < 7 ? payload_len : 7;
    //         sx1262_wait_on_busy();
    //         sx1262_nss_low();
    //         sx1262_spi_transfer(SX1262_CMD_READ_BUFFER);
    //         sx1262_spi_transfer(buffer_offset);  // start offset
    //         sx1262_spi_transfer(0x00);           // NOP status byte
    //         for (uint8_t i = 0; i < read_len; i++)
    //             rx_buf[i] = sx1262_spi_transfer(0x00);
    //         sx1262_nss_high();
    //
    //         // Step E: Parse command and update mute flag
    //         std::memcpy(system.last_rf_command, rx_buf,
    //                     sizeof(system.last_rf_command));
    //
    //         if (std::strncmp(system.last_rf_command, "CMD_OFF", 7) == 0)
    //             system.telemetry_muted = true;
    //         else if (std::strncmp(system.last_rf_command, "CMD_ON", 6) == 0)
    //             system.telemetry_muted = false;
    //
    //         // Step F: Clear IRQ flags before next cycle
    //         uint8_t clr_args[] = {0xFF, 0xFF};
    //         sx1262_write_command(0x02, clr_args, 2); // ClearIrqStatus = 0x02
    //     }
    // }
    // ----------------------------------------------------------------
}

// ---------------------------------------------------------------------------
// Rate gate — respects both the time interval AND the RF mute flag
// ---------------------------------------------------------------------------
bool shouldSendTelemetry(RocketSystem &system)
{
    // Respect the RF command mute flag (Point 4)
    if(system.telemetry_muted)
    {
        return false;
    }

    if(!system.flight_telemetry_enabled &&
       !system.recovery_beacon_enabled)
    {
        return false;
    }

    if(FlightConfig::TELEMETRY_INTERVAL_SECONDS <= 0.0f)
    {
        return true;
    }

    const float elapsed_since_last_packet =
        system.mission_elapsed_seconds -
        system.last_telemetry_time_seconds;

    return elapsed_since_last_packet >=
           FlightConfig::TELEMETRY_INTERVAL_SECONDS;
}

// ---------------------------------------------------------------------------
// Packet builder — transport-neutral snapshot of current system state
// ---------------------------------------------------------------------------
TelemetryPacket buildTelemetryPacket(RocketSystem &system)
{
    TelemetryPacket packet =
    {
        system.telemetry_sequence,
        system.mission_elapsed_seconds,
        system.current_state,
        system.current_flight_phase,
        readAltitude(system),
        system.vertical_velocity,
        system.vertical_acceleration,
        system.system_armed,
        system.fault_detected,
        system.descending,
        system.payload_deployed
    };

    return packet;
}

// ---------------------------------------------------------------------------
// Main telemetry send function
// Handles: CSV file write (Points 2 & 3) + LoRa radio transmission (Point 1)
// ---------------------------------------------------------------------------
void sendTelemetry(RocketSystem &system)
{
    TelemetryPacket packet = buildTelemetryPacket(system);

    // POINT 3: Competition-compliant filename: Flight_<TEAM_ID>.csv
    char filename[64];
    std::snprintf(filename, sizeof(filename),
                  "Flight_%s.csv", FlightConfig::TEAM_ID);

    std::ios_base::openmode mode = std::ios::app;
    if(packet.sequence == 0)
    {
        mode = std::ios::trunc; // overwrite at start of new mission
    }

    std::ofstream out(filename, mode);
    if(!out)
    {
        std::cerr << "[TELEMETRY] ERROR: Failed to open CSV file: "
                  << filename << "\n";
        return;
    }

    // Write CSV header on the very first packet
    if(packet.sequence == 0)
    {
        out << "TEAM ID,TIME STAMPING,PACKET COUNT,ALTITUDE,"
               "PRESSURE,TEMP,VOLTAGE,GNSS TIME,GNSS LATITUDE,"
               "GNSS LONGITUDE,GNSS ALTITUDE,GNSS SATS,"
               "ACCELEROMETER DATA,GYRO SPIN RATE,"
               "FLIGHT SOFTWARE STATE,ANY OPTIONAL DATA,CHECKSUM\n";
    }

    // Telemetry Validation Checks (Task 5.13)
    // Prevent impossible values from being encoded into the RF string.
    float safe_pressure = (system.pressure < 0) ? 0 : system.pressure;
    float safe_voltage = (system.voltage < 0) ? 0 : system.voltage;
    float safe_altitude = (packet.altitude < -1000.0f) ? -1000.0f : packet.altitude;

    char buffer[256];
    int len = std::snprintf(buffer, sizeof(buffer),
        "%s,%.2f,%u,%.2f,%.2f,%.2f,%.2f,%u,%.6f,%.6f,%.2f,%d,%.2f,%.2f,%s,%s",
        FlightConfig::TEAM_ID,
        packet.mission_time_seconds,
        packet.sequence,
        safe_altitude,
        safe_pressure,
        system.temperature,
        safe_voltage,
        system.gnss_time,
        system.gnss_latitude,
        system.gnss_longitude,
        system.gnss_altitude,
        system.gnss_sats,
        packet.vertical_acceleration,
        system.gyro_spin_rate,
        stateName(static_cast<State>(packet.state)),
        flightPhaseName(static_cast<FlightPhase>(packet.flight_phase)));

    // Telemetry Error Handling / Checksum (Task 5.11)
    unsigned char checksum = 0;
    for (int i = 0; i < len; ++i) {
        checksum ^= static_cast<unsigned char>(buffer[i]);
    }

    out << buffer << "," << static_cast<int>(checksum) << "\n";
    out.close();

    // POINT 1: Also transmit the same ASCII CSV row over the LoRa radio.
    // On desktop this is a no-op stub; on STM32 it calls HAL_UART_Transmit.
    if(len > 0)
    {
        transmitOverLoRa(buffer, static_cast<unsigned int>(len));
    }

    // Flash backup every 10 packets (Task 7.5)
    if (packet.sequence % 10 == 0)
    {
        extern bool saveSystemState(const RocketSystem &system);
        saveSystemState(system);
    }

    system.telemetry_sequence++;
    system.last_telemetry_time_seconds = system.mission_elapsed_seconds;
}
