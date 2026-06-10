#pragma once

#include "system.hpp"

struct TelemetryPacket
{
    unsigned int sequence;
    float mission_time_seconds;
    State state;
    float altitude;
    float pressure;
    float temperature;
    float voltage;
    uint32_t gnss_time;
    float gnss_latitude;
    float gnss_longitude;
    float gnss_altitude;
    uint8_t gnss_sats;
    float accel_x;
    float accel_y;
    float accel_z;
    float roll;
    float pitch;
    float yaw;
    float vertical_velocity;
    float vertical_acceleration;
    float gyro_spin_rate; // Raw gyro spin rate around the body Z axis
    uint8_t flags;
    char optional_data[64];
};

bool shouldSendTelemetry(RocketSystem &system);
TelemetryPacket buildTelemetryPacket(RocketSystem &system);
void sendTelemetry(RocketSystem &system);
void receiveRFCommands(RocketSystem &system); // RF uplink command handler
void sx1262_init();                           // one-time SX1262 radio setup (call in BOOT state)
