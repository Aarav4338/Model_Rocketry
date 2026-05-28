#pragma once

#include "system.hpp"

struct TelemetryPacket
{
    unsigned int sequence;
    float mission_time_seconds;
    State state;
    FlightPhase flight_phase;
    float altitude;
    float vertical_velocity;
    float vertical_acceleration;
    bool armed;
    bool fault;
    bool descending;
    bool payload_deployed;
};

bool shouldSendTelemetry(RocketSystem &system);
TelemetryPacket buildTelemetryPacket(RocketSystem &system);
void sendTelemetry(RocketSystem &system);
void receiveRFCommands(RocketSystem &system); // RF uplink command handler
void sx1262_init();                           // one-time SX1262 radio setup (call in BOOT state)
