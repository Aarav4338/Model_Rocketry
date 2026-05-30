#include "telemetry.hpp"

#include <iostream>
#include <fstream>

#include "config.hpp"
#include "sensors.hpp"

static const char *stateName(State state)
{
    switch(state)
    {
        case BOOT:
            return "BOOT";

        case TEST_MODE:
            return "TEST_MODE";

        case Prelaunch_Check:
            return "Prelaunch_Check";

        case Launch_Pad:
            return "Launch_Pad";

        case Ascent:
            return "Ascent";

        case Apogee_confirm:
            return "Apogee_confirm";

        case Payload_Separation:
            return "Payload_Separation";

        case Descent:
            return "Descent";

        case Landed:
            return "Landed";

        case Beacon:
            return "Beacon";

        default:
            return "UNKNOWN";
    }
}

static const char *flightPhaseName(FlightPhase phase)
{
    switch(phase)
    {
        case Flight_Grounded:
            return "Grounded";

        case Flight_Powered_Ascent:
            return "Powered ascent";

        case Flight_Coast:
            return "Coast";

        case Flight_Ballistic_Descent:
            return "Ballistic descent";

        case Flight_Landed:
            return "Landed";

        default:
            return "Unknown";
    }
}

// Decides whether the current loop should emit a packet. Telemetry is
// rate-limited through configuration so console output does not dominate the
// desktop simulation timing.
bool shouldSendTelemetry(RocketSystem &system)
{
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

// Snapshots the current mission and physics-derived values into a transport-
// neutral packet. The desktop build prints this packet, but an embedded target
// could serialize the same structure over UART, USB, CAN, or radio.
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

// Emits a telemetry packet to the desktop console. This function owns output
// formatting so state handlers and simulation code never depend on transport
// details.
void sendTelemetry(RocketSystem &system)
{
    TelemetryPacket packet =
        buildTelemetryPacket(system);

    std::ios_base::openmode mode = std::ios::app;
    if (packet.sequence == 0) {
        mode = std::ios::trunc;
    }
    
    std::ofstream out("data.csv", mode);
    if (!out) {
        std::cerr << "Failed to open CSV file for telemetry\n";
        return;
    }

    if (packet.sequence == 0) {
        out << "TEAM ID,TIME STAMPING,PACKET COUNT,ALTITUDE,PRESSURE,TEMP,VOLTAGE,GNSS TIME,GNSS LATITUDE,GNSS LONGITUDE,GNSS ALTITUDE,GNSS SATS,ACCELEROMETER DATA,GYRO SPIN RATE,FLIGHT SOFTWARE STATE,ANY OPTIONAL DATA,CHECKSUM\n";
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

    // Flash backup every 10 packets (Task 7.5)
    if (packet.sequence % 10 == 0) {
        extern bool saveSystemState(const RocketSystem &system);
        saveSystemState(system);
    }

    system.telemetry_sequence++;
    system.last_telemetry_time_seconds =
        system.mission_elapsed_seconds;
}
