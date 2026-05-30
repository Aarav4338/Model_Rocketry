#include "telecommand.hpp"
#include "logging.hpp"
#include "hal.hpp"
#include <cstring>
#include <cstdio>

void processTelecommand(RocketSystem &system, const char *command)
{
    if (command == nullptr) return;

    char ack_message[FlightConfig::EVENT_MESSAGE_CAPACITY];

    // Command: Telemetry ON
    if (std::strcmp(command, "CMD_TLM_ON") == 0)
    {
        system.flight_telemetry_enabled = true;
        std::snprintf(ack_message, sizeof(ack_message), "ACK: Telemetry Enabled");
        logEvent(system, ack_message, Event_System);
        return;
    }

    // Command: Telemetry OFF
    if (std::strcmp(command, "CMD_TLM_OFF") == 0)
    {
        system.flight_telemetry_enabled = false;
        std::snprintf(ack_message, sizeof(ack_message), "ACK: Telemetry Disabled");
        logEvent(system, ack_message, Event_System);
        return;
    }

    // Safety Check: Flight-altering commands are strictly forbidden unless grounded.
    if (system.current_flight_phase != Flight_Grounded)
    {
        std::snprintf(ack_message, sizeof(ack_message), "REJECTED: %s (Vehicle in flight)", command);
        logEvent(system, ack_message, Event_Fault);
        return;
    }

    // Command: Reset Altitude Reference
    if (std::strcmp(command, "CMD_RST_ALT") == 0)
    {
        system.launch_reference_altitude = system.filtered_altitude;
        std::snprintf(ack_message, sizeof(ack_message), "ACK: Altitude Zeroed (Ref: %.2fm)", system.launch_reference_altitude);
        logEvent(system, ack_message, Event_System);
        return;
    }

    // Command: Reset Accelerometer / IMU Calibration
    if (std::strcmp(command, "CMD_RST_ACC") == 0)
    {
        Hardware::initPrimaryIMU(); // Re-trigger hardware calibration
        system.vertical_acceleration = 0.0f;
        system.vertical_velocity = 0.0f;
        std::snprintf(ack_message, sizeof(ack_message), "ACK: IMU Recalibrated");
        logEvent(system, ack_message, Event_System);
        return;
    }

    // Unknown Command
    std::snprintf(ack_message, sizeof(ack_message), "NACK: Unknown Command (%s)", command);
    logEvent(system, ack_message, Event_Fault);
}
