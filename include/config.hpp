#pragma once

namespace FlightConfig
{
// Competition Specifics
constexpr const char* TEAM_ID = "2026 IN-SPACe-PVC";

// PVC Rocket Physical Specifications (from guidelines)
// Body: PVC, Max Mass (including motor): 11.5Kg
// Motor: 94mm dia x 313mm length, ~3Kg
// Payload: CAN-7U SAT, 1.0Kg (+/- 0.05Kg)
constexpr float MAX_TAKEOFF_MASS_KG = 11.5f;
constexpr float MOTOR_MASS_KG = 3.0f;
constexpr float PAYLOAD_MASS_KG = 1.0f;

constexpr float FILTER_PREVIOUS_WEIGHT = 0.7f;
constexpr float FILTER_RAW_WEIGHT = 0.3f;
constexpr float FILTER_ALTITUDE_ZERO_EPSILON_METERS = 0.001f;

constexpr float SIM_GRAVITY_MPS2 = 9.80665f;
constexpr float SIM_THRUST_ACCELERATION_MPS2 = 52.5f;
constexpr float SIM_MOTOR_BURN_DURATION_SECONDS = 3.0f;
constexpr float SIM_GROUND_ALTITUDE_METERS = 0.0f;
constexpr float SIM_PARACHUTE_DESCENT_VELOCITY_MPS = -3.5f; // Complies with 2 to 5 m/s requirement
constexpr long SIM_MOTOR_IGNITION_DELAY_MILLISECONDS = 5000;
constexpr long MAIN_LOOP_SLEEP_MILLISECONDS = 0;

// Adds a realistic level of barometric noise to make FSM filtering work harder
// and to provide varied altitude readings for each simulation run.
constexpr float SENSOR_NOISE_AMPLITUDE_METERS = 0.2f;

constexpr float VELOCITY_APOGEE_THRESHOLD_MPS = -1.0f;
constexpr float LAUNCH_DETECTION_VELOCITY_MPS = 15.0f;
constexpr float LAUNCH_DETECTION_ALTITUDE_DELTA_METERS = 10.0f;
constexpr float DESCENT_DETECTION_VELOCITY_MPS = 0.0f;
constexpr float MIN_VELOCITY_DELTA_TIME_SECONDS = 0.000001f;
constexpr float LANDING_STATIONARY_VELOCITY_MPS = 1.5f;
constexpr long LANDING_STATIONARY_DURATION_MILLISECONDS = 2000;

constexpr float TELEMETRY_INTERVAL_SECONDS = 0.25f;

constexpr long TEST_MODE_DURATION_SECONDS = 3;
constexpr long PRELAUNCH_CHECK_DURATION_SECONDS = 4;
constexpr long APOGEE_CONFIRM_DURATION_MILLISECONDS = 750;
constexpr long PAYLOAD_SEPARATION_DURATION_SECONDS = 3;
constexpr long LANDED_DURATION_SECONDS = 2;
constexpr long BEACON_DURATION_SECONDS = 3;

constexpr long WATCHDOG_BOOT_TIMEOUT_SECONDS = 5;
constexpr long WATCHDOG_TEST_MODE_TIMEOUT_SECONDS = 10;
constexpr long WATCHDOG_PRELAUNCH_TIMEOUT_SECONDS = 12;
constexpr long WATCHDOG_LAUNCH_PAD_TIMEOUT_SECONDS = 15;
constexpr long WATCHDOG_ASCENT_TIMEOUT_SECONDS = 30;
constexpr long WATCHDOG_APOGEE_CONFIRM_TIMEOUT_SECONDS = 8;
constexpr long WATCHDOG_PAYLOAD_SEPARATION_TIMEOUT_SECONDS = 10;
constexpr long WATCHDOG_DESCENT_TIMEOUT_SECONDS = 600; // Increased to allow a full 1000m descent at 2 m/s
constexpr long WATCHDOG_LANDED_TIMEOUT_SECONDS = 8;
constexpr long WATCHDOG_BEACON_TIMEOUT_SECONDS = 8;

constexpr int ERROR_MESSAGE_CAPACITY = 96;
constexpr int MISSION_LOG_CAPACITY = 4096;
constexpr int MISSION_HISTORY_CAPACITY = 64;
constexpr int EVENT_MESSAGE_CAPACITY = 96;
}
