#include "telecommand.hpp"
#include "logging.hpp"
#include "hal.hpp"
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cctype>
#include <sstream>

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

// Helper: trim whitespace in-place
static inline void trimString(std::string &s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    s = s.substr(a, b - a);
}

// Helper: convert State enum to string (small mapping to avoid depending on telemetry.cpp internals)
static const char* stateToString(State st)
{
    switch(st)
    {
        case BOOT: return "BOOT";
        case TEST_MODE: return "TEST_MODE";
        case Prelaunch_Check: return "Prelaunch_Check";
        case Launch_Pad: return "LAUNCH_PAD";
        case Ascent: return "ASCENT";
        case Apogee_confirm: return "APOGEE_CONFIRM";
        case Payload_Separation: return "PAYLOAD_SEPARATION";
        case Descent: return "DESCENT";
        case Landed: return "LANDED";
        case Beacon: return "BEACON";
        default: return "UNKNOWN";
    }
}

// Send a JSON ACK/NACK over the existing telemetry transmit path
static void sendAck(RocketSystem &system, const char *cmd, bool ack, const char *extra = nullptr)
{
    char out[256];
    float battery = Hardware::readBatteryVoltage();
    const char *state = stateToString(system.current_state);
    if (extra)
        std::snprintf(out, sizeof(out), "{\"ack\":%s,\"cmd\":\"%s\",\"state\":\"%s\",\"battery\":%.2f,\"info\":\"%s\"}",
                      ack ? "true" : "false", cmd, state, battery, extra);
    else
        std::snprintf(out, sizeof(out), "{\"ack\":%s,\"cmd\":\"%s\",\"state\":\"%s\",\"battery\":%.2f}",
                      ack ? "true" : "false", cmd, state, battery);

    Hardware::transmitTelemetry(out, static_cast<uint16_t>(std::strlen(out)));
}

// Process a transport-neutral packet like "CMD:START_TELEMETRY"
void processTelecommandPacket(RocketSystem &system, const char *packet)
{
    if (packet == nullptr) return;

    std::string p(packet);
    trimString(p);
    if (p.size() == 0) return;

    // Backwards compat: if packet does not use CMD: prefix, forward to old parser
    const std::string prefix = "CMD:";
    if (p.rfind(prefix, 0) != 0) {
        processTelecommand(system, p.c_str());
        return;
    }

    std::string cmd = p.substr(prefix.size());
    trimString(cmd);

    // Map to canonical command tokens (uppercase)
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c){ return std::toupper(c); });

    // Flight safety: only allow altering commands on safe states
    bool safe_state = (system.current_state == BOOT) ||
                      (system.current_state == TEST_MODE) ||
                      (system.current_state == Launch_Pad);

    // Block certain commands after ascent
    bool after_ascent = (system.current_state >= Ascent);

    if (cmd == "START_TELEMETRY")
    {
        system.flight_telemetry_enabled = true;
        sendAck(system, "START_TELEMETRY", true);
        return;
    }

    if (cmd == "STOP_TELEMETRY")
    {
        system.flight_telemetry_enabled = false;
        sendAck(system, "STOP_TELEMETRY", true);
        return;
    }

    if (cmd == "ZERO_SENSORS")
    {
        if (!safe_state || after_ascent)
        {
            sendAck(system, "ZERO_SENSORS", false, "unsafe_state");
            return;
        }
        system.launch_reference_altitude = system.filtered_altitude;
        Hardware::initPrimaryIMU();
        sendAck(system, "ZERO_SENSORS", true);
        return;
    }

    if (cmd == "ARM_FLIGHT")
    {
        if (!safe_state)
        {
            sendAck(system, "ARM_FLIGHT", false, "unsafe_state");
            return;
        }
        system.system_armed = true;
        sendAck(system, "ARM_FLIGHT", true);
        return;
    }

    if (cmd == "DISARM_FLIGHT")
    {
        if (!safe_state || after_ascent)
        {
            sendAck(system, "DISARM_FLIGHT", false, "unsafe_state");
            return;
        }
        system.system_armed = false;
        sendAck(system, "DISARM_FLIGHT", true);
        return;
    }

    if (cmd == "PING")
    {
        // reply with PONG in extra field
        sendAck(system, "PING", true, "PONG");
        return;
    }

    if (cmd == "REQUEST_STATUS")
    {
        // Build a small status JSON
        char out[256];
        const char *state = stateToString(system.current_state);
        float battery = Hardware::readBatteryVoltage();
        bool radio = Hardware::radioIsReady();
        bool sensors = system.IMU_Ready && system.Telemetry_Ready;
        bool tlm = system.flight_telemetry_enabled;
        std::snprintf(out, sizeof(out), "{\"ack\":true,\"cmd\":\"REQUEST_STATUS\",\"state\":\"%s\",\"battery\":%.2f,\"telemetry_enabled\":%s,\"radio_ready\":%s,\"sensors_ok\":%s}",
                      state, battery, tlm ? "true" : "false", radio ? "true" : "false", sensors ? "true" : "false");
        Hardware::transmitTelemetry(out, static_cast<uint16_t>(std::strlen(out)));
        return;
    }

    // Unknown command
    sendAck(system, cmd.c_str(), false, "unknown_command");
}
