#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "config.hpp"

enum State
{
    BOOT,
    TEST_MODE,
    Prelaunch_Check,
    Launch_Pad,
    Ascent,
    Apogee_confirm,
    Payload_Separation,
    Descent,
    Landed,
    Beacon
};

enum FlightPhase
{
    Flight_Grounded,
    Flight_Powered_Ascent,
    Flight_Coast,
    Flight_Ballistic_Descent,
    Flight_Landed
};

enum EventCategory
{
    Event_System,
    Event_State,
    Event_Flight,
    Event_Deployment,
    Event_Fault
};

struct MissionEvent
{
    float timestamp_seconds;
    EventCategory category;
    char message[FlightConfig::EVENT_MESSAGE_CAPACITY];
};

struct RocketSystem
{
    State current_state;
    State previous_state;
    FlightPhase current_flight_phase;

    bool system_armed;
    bool descending;

    bool fault_detected;
    bool sensor_failure;
    bool watchdog_enabled;

    bool payload_deployed;
    bool mission_complete;
    bool thrust_active;
    bool burnout_detected;
    bool parachute_failure_detected;

    bool imu_powered;
    bool high_rate_logging_enabled;
    bool flight_telemetry_enabled;
    bool recovery_beacon_enabled;
    bool landed_power_saving_applied;

    float raw_altitude;
    float filtered_altitude;
    float previous_altitude;
    float vertical_velocity;
    float vertical_acceleration;
    float last_filtered_altitude_for_velocity;
    float simulated_true_altitude;
    float simulated_vertical_velocity;
    float motor_burn_time_remaining;
    float launch_reference_altitude;
    float landing_stationary_time_seconds;
    float max_descent_velocity;

    // Additional sensor data for IN-SPACe telemetry
    float pressure;
    float temperature;
    float voltage;
    uint32_t gnss_time;
    double gnss_latitude;
    double gnss_longitude;
    float gnss_altitude;
    uint8_t gnss_sats;
    float gyro_spin_rate;

    unsigned int simulation_step;
    unsigned int telemetry_sequence;

    float delta_time_seconds;
    float mission_elapsed_seconds;
    float state_entry_time_seconds;
    float last_telemetry_time_seconds;

    char error_message[FlightConfig::ERROR_MESSAGE_CAPACITY];

    char mission_log[FlightConfig::MISSION_LOG_CAPACITY];
    std::size_t mission_log_length;

    MissionEvent mission_history[FlightConfig::MISSION_HISTORY_CAPACITY];
    std::size_t mission_event_count;

    std::chrono::steady_clock::time_point mission_start_time;
    std::chrono::steady_clock::time_point previous_update_time;
    std::chrono::steady_clock::time_point current_time;
    std::chrono::steady_clock::time_point state_entry_time;
};
