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

    // Open file in append mode
    std::ofstream out("Flight_2026_IN-SPACe-PVC.csv", std::ios::app);
    if (!out) {
        std::cerr << "Failed to open CSV file for telemetry\n";
        return;
    }

    // <TEAM ID>, <TIME STAMPING>, <PACKET COUNT>, <ALTITUDE>, <PRESSURE>,
    // <TEMP>, <VOLTAGE>, <GNSS TIME>, <GNSS LATITUDE>, <GNSS LONGITUDE>,
    // <GNSS ALTITUDE>, <GNSS SATS>, <ACCELEROMETER DATA>, <GYRO SPIN RATE>,
    // <FLIGHT SOFTWARE STATE>, <ANY OPTIONAL DATA>
    
    out << FlightConfig::TEAM_ID << ","
        << packet.mission_time_seconds << ","
        << packet.sequence << ","
        << packet.altitude << ","
        << system.pressure << ","
        << system.temperature << ","
        << system.voltage << ","
        << system.gnss_time << ","
        << system.gnss_latitude << ","
        << system.gnss_longitude << ","
        << system.gnss_altitude << ","
        << system.gnss_sats << ","
        << packet.vertical_acceleration << ","
        << system.gyro_spin_rate << ","
        << stateName(static_cast<State>(packet.state)) << ","
        << flightPhaseName(static_cast<FlightPhase>(packet.flight_phase))
        << "\n";

    out.close();

    system.telemetry_sequence++;
    system.last_telemetry_time_seconds =
        system.mission_elapsed_seconds;
}
