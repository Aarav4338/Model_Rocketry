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
constexpr long MAIN_LOOP_SLEEP_MILLISECONDS = 20;

// Adds a realistic level of barometric noise to make FSM filtering work harder
// and to provide varied altitude readings for each simulation run.
constexpr float SENSOR_NOISE_AMPLITUDE_METERS = 0.2f;

// Velocity threshold for apogee detection (m/s).
// Set to -2.0 rather than 0.0 to add hysteresis: the rocket must be
// clearly descending, not oscillating around zero due to baro noise.
// (Flaw 6 fix — was 0.0f)
constexpr float VELOCITY_APOGEE_THRESHOLD_MPS = -2.0f;
constexpr float LAUNCH_DETECTION_VELOCITY_MPS = 2.0f;
constexpr float LAUNCH_DETECTION_ALTITUDE_DELTA_METERS = 0.5f;
constexpr float DESCENT_DETECTION_VELOCITY_MPS = 0.0f;
constexpr float MIN_VELOCITY_DELTA_TIME_SECONDS = 0.000001f;
constexpr float LANDING_STATIONARY_VELOCITY_MPS = 1.5f;
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
constexpr float APOGEE_CONFIRM_CLIMB_DEBOUNCE_SECONDS = 0.25f;
constexpr float APOGEE_CONFIRM_EMERGENCY_DESCENT_VELOCITY_MPS = -20.0f;

constexpr long PAYLOAD_SEPARATION_DURATION_SECONDS = 3;
constexpr int DEPLOYMENT_MAX_RETRIES = 3;
constexpr float DEPLOYMENT_RETRY_INTERVAL_SECONDS = 0.3f;
constexpr float DEPLOYMENT_TIMEOUT_SECONDS = 1.5f;

constexpr float DESCENT_BALLISTIC_WARNING_VELOCITY_MPS = -40.0f;
constexpr float DESCENT_STABLE_DURATION_SECONDS = 3.0f;
constexpr float LANDING_IMPACT_ACCEL_THRESHOLD_MPS2 = 25.0f;

constexpr long LANDED_DURATION_SECONDS = 2;
constexpr long BEACON_DURATION_SECONDS = 3;

// ---------- Ascent State ----------
// Minimum altitude above the launch reference (m) before apogee detection
// is allowed. Prevents a low-altitude wobble or false launch from triggering
// parachute deployment on the rail.
constexpr float ASCENT_MIN_APOGEE_ALTITUDE_M = 20.0f;

// Time after entering Ascent during which apogee detection is fully suppressed.
// Protects against burnout vibration and filter transients immediately after
// launch confirmation.
constexpr float ASCENT_APOGEE_INHIBIT_SECONDS = 1.5f;

// Apogee candidate conditions must be satisfied continuously for this many
// seconds before the FSM transitions to Apogee_confirm.
// (Flaw 1 fix — replaces instant single-tick transition)
constexpr float ASCENT_APOGEE_DEBOUNCE_SECONDS = 0.3f;

// Number of consecutive ticks for which vertical_velocity must be
// negative (descending) before isDescending() reports true.
// Guards against single-sample baro noise faking a descent.
// (Flaw 2 fix)
constexpr int ASCENT_CONSECUTIVE_DESCENT_TICKS = 5;

// Accelerometer magnitude must be within this band around 1g to be
// counted as near-freefall (engine off, drag only).
// Used in the sensor-fusion apogee gate (Flaw 5 fix).
constexpr float ASCENT_FREEFALL_ACCEL_TOLERANCE_MPS2 = 4.0f;

// ---------- Launch Pad ----------
// Number of altitude samples averaged together to produce the pad
// reference altitude on state entry. More samples = more stable baseline
// against barometric noise spikes.
constexpr int LAUNCH_PAD_ALTITUDE_AVG_SAMPLES = 50;

// How long after entering Launch_Pad to ignore launch detection entirely.
// Protects against false triggers from bumps while placing the rocket on
// the rail or from residual vibration immediately after arming.
constexpr float LAUNCH_PAD_INHIBIT_SECONDS = 2.0f;

// Launch detection conditions must all pass continuously for this many
// seconds before ascent is confirmed. Prevents single-frame spikes
// (IMU bump, barometer glitch) from triggering a false launch.
constexpr float LAUNCH_PAD_DEBOUNCE_SECONDS = 0.1f;

// Maximum time the rocket is allowed to remain in Launch_Pad.
// After this the mission is considered aborted (ignition failure,
// operator cancellation, etc.) and a critical fault is raised.
// Set to 30 minutes = 1800 seconds as requested.
constexpr long LAUNCH_PAD_TIMEOUT_SECONDS = 1800;

constexpr long WATCHDOG_BOOT_TIMEOUT_SECONDS = 5;
constexpr long WATCHDOG_TEST_MODE_TIMEOUT_SECONDS = 10;
constexpr long WATCHDOG_PRELAUNCH_TIMEOUT_SECONDS = 12;
constexpr long WATCHDOG_LAUNCH_PAD_TIMEOUT_SECONDS = 1800;
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
