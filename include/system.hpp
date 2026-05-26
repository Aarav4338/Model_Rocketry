#pragma once

#include <chrono>
#include <cstddef>

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

    // Set true by raiseCriticalFault(). Prelaunch check treats any
    // critical fault as an immediate abort; non-critical (warning)
    // faults from boot (e.g. telemetry degraded) do not block arming.
    bool has_critical_fault;

    bool IMU_Ready = false;
    bool Telemetry_Ready = false;
    bool SD_Card_Ready = false;

    bool payload_deployed;
    bool mission_complete;
    bool thrust_active;
    bool burnout_detected;

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

    // Live IMU readings filled by updateIMUReadings() each tick.
    // Used by prelaunch checks for sanity, stationary, and tilt.
    float imu_accel_magnitude;   // 3-axis accelerometer vector magnitude (m/s²)
    float imu_gyro_rate;         // 3-axis gyroscope vector magnitude (rad/s)
    float tilt_angle_deg;        // estimated tilt from vertical (degrees)

    // Battery voltage read by the power subsystem stub.
    float battery_voltage;

    // Debounce accumulator for prelaunch checks (seconds).
    // Resets to zero whenever any check fails; arming only happens
    // after this reaches PRELAUNCH_DEBOUNCE_SECONDS.
    float prelaunch_conditions_met_seconds;

    unsigned int simulation_step;
    unsigned int telemetry_sequence;

    float delta_time_seconds;
    float mission_elapsed_seconds;
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
    std::chrono::steady_clock::time_point boot_start_time;
};
