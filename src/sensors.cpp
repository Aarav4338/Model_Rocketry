#include "sensors.hpp"

#include "config.hpp"

// Hardware abstraction placeholder for IMU startup. The desktop build always
// succeeds, while a future STM32 port can put HAL sensor initialization here
// without changing the mission FSM.
bool initialize_IMU()
{
    return true;
}

// Hardware abstraction placeholder for the telemetry transport. Keeping the
// init call behind this function prevents states from depending on UART, USB,
// radio, or desktop-console details.
bool initialize_telemetry()
{
    return true;
}

// Hardware abstraction placeholder for mission-log storage. A later SD-card
// driver can live behind this function while `BOOT` keeps the same structure.
bool initialize_SD_card()
{
    return true;
}

// Returns the altitude value approved for mission decisions. This is filtered
// altitude, not raw simulated altitude, so state guards operate on conditioned
// flight data.
float readAltitude(RocketSystem &system)
{
    return system.filtered_altitude;
}

// Reports the filtered-data descent flag used by apogee confirmation. The flag
// is derived by the filter layer and is separate from simulator phase tracking.
bool isDescending(RocketSystem &system)
{
    return system.descending;
}

// Gate for entering flight. The system must be armed, and launch evidence must
// come from filtered altitude movement or derived vertical velocity.
bool canEnterAscent(RocketSystem &system)
{
    return system.system_armed &&
           hasDetectedLaunch(system);
}

// Detects launch from processed flight evidence rather than a mission-state
// timer. The simulator may choose when ignition occurs, but the FSM only sees
// the resulting filtered altitude rise or derived upward velocity.
//
// Both conditions must be satisfied simultaneously (AND, not OR).
// Rationale: a single IMU bump raises velocity but not altitude; a barometer
// glitch raises apparent altitude but not velocity. Requiring agreement from
// both independent channels gives multi-sensor confirmation and prevents
// single-sensor false positives (Flaw 3 fix).
bool hasDetectedLaunch(RocketSystem &system)
{
    const bool velocity_indicates_launch =
        system.vertical_velocity >=
        FlightConfig::LAUNCH_DETECTION_VELOCITY_MPS;

    const bool altitude_indicates_launch =
        (readAltitude(system) -
         system.launch_reference_altitude) >=
        FlightConfig::LAUNCH_DETECTION_ALTITUDE_DELTA_METERS;

    // Both sensors must agree before we trust the evidence.
    return velocity_indicates_launch &&
           altitude_indicates_launch;
}

// Starts apogee confirmation only when filtered-derived data indicates descent.
bool canConfirmApogee(RocketSystem &system)
{
    return isDescending(system);
}

// Detects the natural apogee crossing from derived vertical velocity. No
// hardcoded altitude ceiling is used; the simulator has to produce the motion.
bool hasPassedApogee(RocketSystem &system)
{
    return system.vertical_velocity <=
           FlightConfig::VELOCITY_APOGEE_THRESHOLD_MPS;
}

// Protects deployment by rejecting a candidate apogee if filtered-derived
// velocity turns positive again during the confirmation window.
bool hasResumedClimb(RocketSystem &system)
{
    return system.vertical_velocity >
           FlightConfig::VELOCITY_APOGEE_THRESHOLD_MPS;
}

// Payload deployment remains the explicit prerequisite for entering descent.
// This keeps recovery sequencing separate from physics.
bool canEnterDescent(RocketSystem &system)
{
    return system.payload_deployed;
}

// Landing is based on sustained near-zero derived velocity accumulated by the
// filter layer. The FSM does not require exact altitude equality with ground.
bool hasDetectedLanding(RocketSystem &system)
{
    return system.landing_stationary_time_seconds >=
           (FlightConfig::LANDING_STATIONARY_DURATION_MILLISECONDS /
            1000.0f);
}

// ---------- Prelaunch sensor checks ----------

// Hardware abstraction for live IMU data. On a real embedded build this
// function reads from the IMU driver and computes magnitudes. On the desktop
// simulator the values are filled by updateSimulation() each tick, so this
// stub simply trusts what is already in the struct.
// The separation keeps every prelaunch check isolated from simulation details.
void updateIMUReadings(RocketSystem &system)
{
    // No-op on desktop: simulation.cpp writes directly to the IMU fields
    // (imu_accel_magnitude, imu_gyro_rate, tilt_angle_deg, battery_voltage)
    // before this is called. On hardware, replace this body with driver reads.
    (void)system;
}

// Verifies the accelerometer is producing physically plausible values for a
// rocket standing still on a launch pad. A healthy grounded IMU must read
// close to 1g (9.80665 m/s²). Values outside [MIN, MAX] indicate a broken
// sensor axis, power issue, or completely dead chip.
bool isIMUSane(const RocketSystem &system)
{
    return system.imu_accel_magnitude >=
               FlightConfig::PRELAUNCH_IMU_ACCEL_MIN_MPS2 &&
           system.imu_accel_magnitude <=
               FlightConfig::PRELAUNCH_IMU_ACCEL_MAX_MPS2;
}

// Checks that the rocket is not rotating or being physically moved.
// Two independent criteria must both hold:
//   1. Gyroscope rate is below the stationary threshold (not spinning/carried).
//   2. Accelerometer magnitude is close enough to 1g (not in random motion).
bool isStationary(const RocketSystem &system)
{
    const bool gyro_quiet =
        system.imu_gyro_rate <=
        FlightConfig::PRELAUNCH_IMU_GYRO_STATIONARY_RADS;

    const float accel_error =
        system.imu_accel_magnitude -
        FlightConfig::SIM_GRAVITY_MPS2;

    const bool accel_stable =
        (accel_error < 0.0f ? -accel_error : accel_error) <=
        FlightConfig::PRELAUNCH_ACCEL_1G_TOLERANCE_MPS2;

    return gyro_quiet && accel_stable;
}

// Verifies the battery voltage is above the minimum threshold required for
// a safe flight. A low battery risks an MCU brownout during pyro firing or
// parachute deployment, which would be catastrophic.
bool isBatteryOk(const RocketSystem &system)
{
    return system.battery_voltage >=
           FlightConfig::PRELAUNCH_MIN_BATTERY_VOLTAGE;
}

// Estimates launch-pad verticality from the IMU tilt field. A large tilt
// means the rail has slipped or the rocket is lying on its side; either
// condition makes a safe ascent trajectory impossible.
bool isVertical(const RocketSystem &system)
{
    return system.tilt_angle_deg <=
           FlightConfig::PRELAUNCH_MAX_TILT_DEG;
}
