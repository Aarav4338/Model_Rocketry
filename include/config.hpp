#pragma once

namespace FlightConfig
{
constexpr float FILTER_PREVIOUS_WEIGHT = 0.7f;
constexpr float FILTER_RAW_WEIGHT = 0.3f;
constexpr float FILTER_ALTITUDE_ZERO_EPSILON_METERS = 0.001f;

constexpr float SIM_GRAVITY_MPS2 = 9.80665f;
constexpr float SIM_THRUST_ACCELERATION_MPS2 = 30.0f;
constexpr float SIM_MOTOR_BURN_DURATION_SECONDS = 3.0f;
constexpr float SIM_GROUND_ALTITUDE_METERS = 0.0f;
constexpr long SIM_MOTOR_IGNITION_DELAY_MILLISECONDS = 5000;
constexpr long MAIN_LOOP_SLEEP_MILLISECONDS = 20;

// Kept at zero to preserve the baseline mission behavior while keeping the
// noise-injection architecture ready for controlled tests.
constexpr float SENSOR_NOISE_AMPLITUDE_METERS = 0.0f;

constexpr float VELOCITY_APOGEE_THRESHOLD_MPS = 0.0f;
constexpr float LAUNCH_DETECTION_VELOCITY_MPS = 2.0f;
constexpr float LAUNCH_DETECTION_ALTITUDE_DELTA_METERS = 0.5f;
constexpr float DESCENT_DETECTION_VELOCITY_MPS = 0.0f;
constexpr float MIN_VELOCITY_DELTA_TIME_SECONDS = 0.000001f;
constexpr float LANDING_STATIONARY_VELOCITY_MPS = 0.15f;
constexpr long LANDING_STATIONARY_DURATION_MILLISECONDS = 2000;

constexpr float TELEMETRY_INTERVAL_SECONDS = 0.25f;

constexpr long TEST_MODE_DURATION_SECONDS = 3;

// ---------- Prelaunch Check ----------
// Maximum sane accelerometer magnitude while on the pad (m/s²).
// A healthy grounded IMU should read roughly 1g (9.8). If it reads
// beyond this, the sensor is likely garbage.
constexpr float PRELAUNCH_IMU_ACCEL_MAX_MPS2 = 15.0f;

// Minimum accelerometer magnitude — below this the IMU is probably
// dead or in free-fall, neither acceptable on a pad.
constexpr float PRELAUNCH_IMU_ACCEL_MIN_MPS2 = 7.0f;

// Maximum gyroscope angular-rate magnitude (rad/s) that counts as
// "stationary". If the rocket is spinning or being carried, this
// threshold is exceeded and arming is blocked.
constexpr float PRELAUNCH_IMU_GYRO_STATIONARY_RADS = 0.1f;

// How close the accelerometer magnitude must be to 1g (m/s²)
// for the "stable on pad" check.
constexpr float PRELAUNCH_ACCEL_1G_TOLERANCE_MPS2 = 2.0f;

// All prelaunch checks must pass continuously for this many seconds
// before the system is allowed to arm. Prevents single-glitch arming.
constexpr float PRELAUNCH_DEBOUNCE_SECONDS = 2.0f;

// Minimum battery voltage (V) required to arm. Below this, a
// mid-flight voltage sag could reset the MCU during chute deployment.
constexpr float PRELAUNCH_MIN_BATTERY_VOLTAGE = 7.0f;

// Maximum tilt from vertical (degrees) permitted while arming.
// Checks that the rocket is upright on the rail, not lying sideways.
constexpr float PRELAUNCH_MAX_TILT_DEG = 15.0f;

// Hard timeout for the entire prelaunch check phase (seconds).
// If conditions are never satisfied within this window, a critical
// fault is raised rather than letting the FSM hang indefinitely.
constexpr long PRELAUNCH_TIMEOUT_SECONDS = 10;
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
constexpr long WATCHDOG_DESCENT_TIMEOUT_SECONDS = 30;
constexpr long WATCHDOG_LANDED_TIMEOUT_SECONDS = 8;
constexpr long WATCHDOG_BEACON_TIMEOUT_SECONDS = 8;

constexpr int ERROR_MESSAGE_CAPACITY = 96;
constexpr int MISSION_LOG_CAPACITY = 4096;
constexpr int MISSION_HISTORY_CAPACITY = 64;
constexpr int EVENT_MESSAGE_CAPACITY = 96;
}
