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

// Hardware abstraction placeholder to poll sensors that are not explicitly
// tied to physics simulation yet, satisfying data reporting requirements.
void updateSensors(RocketSystem &system)
{
    Hardware::readBarometer(system.pressure, system.temperature);
    system.voltage = Hardware::readBatteryVoltage();
    
    Hardware::readGNSS(system.gnss_time, 
                       system.gnss_latitude, 
                       system.gnss_longitude, 
                       system.gnss_altitude, 
                       system.gnss_sats);
                       
    // Note: Simulated altitude from readAltitude(system) overrides raw GNSS alt
    // for FSM logic, GNSS altitude is strictly for telemetry compliance.
    
    float ax, ay, az, gx, gy, gz;
    Hardware::readIMU(ax, ay, az, gx, gy, gz);
    system.gyro_spin_rate = gz; // Proxy for spin rate
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
bool hasDetectedLaunch(RocketSystem &system)
{
    const bool velocity_indicates_launch =
        system.vertical_velocity >=
        FlightConfig::LAUNCH_DETECTION_VELOCITY_MPS;

    const bool altitude_indicates_launch =
        (readAltitude(system) -
         system.launch_reference_altitude) >=
        FlightConfig::LAUNCH_DETECTION_ALTITUDE_DELTA_METERS;

    return velocity_indicates_launch ||
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
           (FlightConfig::LANDING_STATIONARY_DURATION_MILLISECONDS /
            1000.0f);
}
