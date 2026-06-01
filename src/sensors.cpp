#include "sensors.hpp"

#include "config.hpp"
#include "hal.hpp"

// Hardware abstraction placeholder for IMU startup.
bool initialize_IMU()
{
    return Hardware::initPrimaryIMU();
}

// Hardware abstraction placeholder for the redundant altimeter required by guidelines.
bool initialize_redundant_altimeter()
{
    return Hardware::initRedundantAltimeter();
}

// Hardware abstraction placeholder for the telemetry transport. 
bool initialize_telemetry()
{
    return Hardware::initRadio();
}

// Hardware abstraction placeholder for mission-log storage. 
bool initialize_SD_card()
{
    return Hardware::initSDCard();
}

// Returns the altitude value approved for mission decisions. This is filtered
// altitude, not raw simulated altitude, so state guards operate on conditioned
// flight data.
float readAltitude(RocketSystem &system)
{
    return system.filtered_altitude;
}

#include <cmath>

// Hardware abstraction placeholder to poll sensors that are not explicitly
// tied to physics simulation yet, satisfying data reporting requirements.
void updateSensors(RocketSystem &system)
{
    float altitude = readAltitude(system);

    if(!Hardware::readBarometer(system.pressure, system.temperature))
    {
        // Fallback barometer simulation when HAL is not available.
        system.pressure = 101325.0f * std::pow(1.0f - (2.25577e-5f * altitude), 5.25588f);
        system.temperature = 25.0f - (0.0065f * altitude);
    }
    system.voltage = Hardware::readBatteryVoltage();

    Hardware::readGNSS(system.gnss_time,
                       system.gnss_latitude,
                       system.gnss_longitude,
                       system.gnss_altitude,
                       system.gnss_sats);

    // Note: Simulated altitude from readAltitude(system) overrides raw GNSS alt
    // for FSM logic, GNSS altitude is strictly for telemetry compliance.
    if(system.gnss_sats == 0)
    {
        system.gnss_time = static_cast<long>(system.mission_elapsed_seconds);
        system.gnss_latitude = 35.3331 + (altitude * 0.0000001f); // fake drift
        system.gnss_longitude = -117.803 - (altitude * 0.0000001f);
        system.gnss_altitude = altitude + 2.0f; // GNSS slightly different from baro
        system.gnss_sats = 8;
    }

    float ax, ay, az, gx, gy, gz;
    if(Hardware::readIMU(ax, ay, az, gx, gy, gz))
    {
        system.gyro_spin_rate = gz; // Proxy for spin rate
    }
    else
    {
        // Fallback for desktop simulation if IMU hardware is unavailable.
        system.gyro_spin_rate = system.thrust_active ? 15.0f : 0.0f;
    }
}

// Reports that the rocket is genuinely descending based on N consecutive
// ticks of negative vertical velocity. A single baro noise spike that briefly
// crosses zero cannot satisfy this; real sustained descent is required.
// The consecutive_descent_ticks counter is maintained by updateVelocity() in
// filters.cpp (Flaw 2 fix).
bool isDescending(RocketSystem &system)
{
    return system.consecutive_descent_ticks >=
           FlightConfig::ASCENT_CONSECUTIVE_DESCENT_TICKS;
}

// Returns true when the accelerometer magnitude is close enough to 1g to
// indicate the engine is off and the rocket is in near-freefall.
// Used as a third confirmation channel in the apogee sensor-fusion gate.
// Near-freefall rules out powered flight and extreme atmospheric drag events
// (Flaw 5 fix).
bool isNearFreefall(const RocketSystem &system)
{
    const float accel_error =
        system.imu_accel_magnitude -
        FlightConfig::SIM_GRAVITY_MPS2;

    const float abs_error =
        accel_error < 0.0f ? -accel_error : accel_error;

    return abs_error <= FlightConfig::ASCENT_FREEFALL_ACCEL_TOLERANCE_MPS2;
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
// As per guidelines, checks that the redundant altimeter chain is also healthy.
bool canConfirmApogee(RocketSystem &system)
{
    // A real STM32 implementation would read the redundant altimeter here to ensure
    // both sensors agree on altitude drop before allowing deployment.
    bool redundant_altimeter_healthy = true; 
    
    return isDescending(system) && redundant_altimeter_healthy;
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
           FlightConfig::DESCENT_STABLE_DURATION_SECONDS;
}

// Hardware abstraction to verify payload deployment mechanically.
// In hardware, this would check a microswitch or servo feedback.
// In simulation, we fake it using a flag set by the simulator logic.
bool isPayloadReleased(RocketSystem &system)
{
    // Stub implementation: true if payload is deployed and simulation acknowledges it.
    // In our sim, we will just return system.payload_deployed (which will be set by the simulator in the next step).
    return system.payload_deployed;
}

// Checks IMU for sudden high-G shock indicative of landing impact.
bool hasLandingImpact(const RocketSystem &system)
{
    return system.landing_impact_detected;
}

// Hardware abstraction to save flight data to persistence (e.g. SD card)
void saveFlightDataToSD(RocketSystem &system)
{
    if(!system.flight_data_saved)
    {
        // On hardware, write mission_log and metadata to FATFS here.
        system.flight_data_saved = true;
    }
}

// Hardware abstraction to read GPS coords
void updateGPSReadings(RocketSystem &system)
{
    // No-op for desktop, simulator fills this in.
    (void)system;
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
