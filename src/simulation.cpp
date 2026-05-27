#include "simulation.hpp"

#include "config.hpp"
#include "logging.hpp"
#include "timing.hpp"

static bool isAtGround(const RocketSystem &system)
{
    return system.simulated_true_altitude <=
           FlightConfig::SIM_GROUND_ALTITUDE_METERS &&
           system.simulated_vertical_velocity <= 0.0f;
}

static bool shouldIgniteMotor(RocketSystem &system)
{
    return system.current_state == Launch_Pad &&
           system.system_armed &&
           !system.thrust_active &&
           !system.burnout_detected &&
           stateElapsedMilliseconds(system) >=
           FlightConfig::SIM_MOTOR_IGNITION_DELAY_MILLISECONDS;
}

static void igniteMotor(RocketSystem &system)
{
    system.thrust_active = true;
    system.motor_burn_time_remaining =
        FlightConfig::SIM_MOTOR_BURN_DURATION_SECONDS;
    system.current_flight_phase = Flight_Powered_Ascent;

    logEvent(system,
             "MOTOR IGNITION",
             Event_Flight);
}

static bool shouldIntegrateFlightMotion(const RocketSystem &system)
{
    return system.thrust_active ||
           system.simulated_true_altitude >
           FlightConfig::SIM_GROUND_ALTITUDE_METERS ||
           system.simulated_vertical_velocity != 0.0f;
}

static float netVerticalAcceleration(const RocketSystem &system)
{
    if(!shouldIntegrateFlightMotion(system))
    {
        return 0.0f;
    }

    if(system.thrust_active)
    {
        return FlightConfig::SIM_THRUST_ACCELERATION_MPS2 -
               FlightConfig::SIM_GRAVITY_MPS2;
    }

    return -FlightConfig::SIM_GRAVITY_MPS2;
}

static void integrateVerticalMotion(RocketSystem &system)
{
    if(!shouldIntegrateFlightMotion(system))
    {
        system.vertical_acceleration = 0.0f;
        return;
    }

    system.vertical_acceleration =
        netVerticalAcceleration(system);

    system.simulated_vertical_velocity +=
        system.vertical_acceleration *
        system.delta_time_seconds;

    if (system.current_state >= Descent && system.simulated_vertical_velocity < FlightConfig::SIM_PARACHUTE_DESCENT_VELOCITY_MPS)
    {
        system.simulated_vertical_velocity = FlightConfig::SIM_PARACHUTE_DESCENT_VELOCITY_MPS;
        system.vertical_acceleration = 0.0f;
    }

    system.simulated_true_altitude +=
        system.simulated_vertical_velocity *
        system.delta_time_seconds;

    if(isAtGround(system))
    {
        system.simulated_true_altitude =
            FlightConfig::SIM_GROUND_ALTITUDE_METERS;
        system.simulated_vertical_velocity = 0.0f;
        system.vertical_acceleration = 0.0f;
    }
}

static void updateMotorBurn(RocketSystem &system)
{
    if(!system.thrust_active)
    {
        return;
    }

    system.motor_burn_time_remaining -=
        system.delta_time_seconds;

    if(system.motor_burn_time_remaining > 0.0f)
    {
        return;
    }

    system.motor_burn_time_remaining = 0.0f;
    system.thrust_active = false;
    system.burnout_detected = true;

    logEvent(system,
             "MOTOR BURNOUT DETECTED",
             Event_Flight);
}

static void updateFlightPhase(RocketSystem &system)
{
    if(system.thrust_active)
    {
        system.current_flight_phase = Flight_Powered_Ascent;
        return;
    }

    if(system.burnout_detected &&
       isAtGround(system))
    {
        system.current_flight_phase = Flight_Landed;
        return;
    }

    if(!system.burnout_detected &&
       isAtGround(system))
    {
        system.current_flight_phase = Flight_Grounded;
        return;
    }

    if(system.simulated_vertical_velocity > 0.0f)
    {
        system.current_flight_phase = Flight_Coast;
        return;
    }

    system.current_flight_phase = Flight_Ballistic_Descent;
}

#include <cstdlib>

static float simulatedNoiseSample(const RocketSystem &system)
{
    // Generate a random float between -1.0 and 1.0 to simulate barometric white noise
    float random_factor = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
    return random_factor * FlightConfig::SENSOR_NOISE_AMPLITUDE_METERS;
}

// Fills the IMU and power stub fields used by prelaunch checks.
// While grounded the accelerometer reads ~1g (gravity only) and the
// gyroscope reads ~0 rad/s (stationary). During powered ascent the
// accelerometer magnitude rises by the thrust component. Tilt is held
// at a small fixed angle representing a well-aligned rail. Battery
// voltage is a static nominal value for the desktop sim.
static void simulateIMUAndPower(RocketSystem &system)
{
    // Grounded: gravity vector only, IMU reads ~1g on the Z axis.
    // In flight: add the simulated vertical acceleration magnitude.
    const float flight_accel =
        system.thrust_active
            ? FlightConfig::SIM_THRUST_ACCELERATION_MPS2
            : 0.0f;

    system.imu_accel_magnitude =
        FlightConfig::SIM_GRAVITY_MPS2 + flight_accel;

    // Gyroscope: rocket is stationary on the pad and during coast.
    // A real sensor would have slight bias noise; kept at zero here
    // so the desktop run passes the stationary threshold cleanly.
    system.imu_gyro_rate = 0.0f;

    // Tilt: 2 degrees off-vertical — a well-aligned launch rail.
    system.tilt_angle_deg = 2.0f;

    // Battery: nominal 11.4 V (3S LiPo at ~3.8 V/cell).
    system.battery_voltage = 11.4f;
}

// Advances the desktop physics model by one loop delta. The simulation owns
// true acceleration, true velocity, motor burn state, and true altitude; it then
// exposes only raw sensor-like altitude to the rest of the avionics pipeline.
void updateSimulation(RocketSystem &system, SimulationScenario scenario)
{
    if(shouldIgniteMotor(system))
    {
        igniteMotor(system);
    }

    if (scenario == SCENARIO_MOTOR_FAILURE && system.thrust_active && system.motor_burn_time_remaining < FlightConfig::SIM_MOTOR_BURN_DURATION_SECONDS * 0.5f)
    {
        // Simulate motor dying halfway through
        system.motor_burn_time_remaining = 0.0f;
    }

    integrateVerticalMotion(system);

    if (scenario == SCENARIO_PARACHUTE_FAILURE && system.simulated_vertical_velocity == FlightConfig::SIM_PARACHUTE_DESCENT_VELOCITY_MPS)
    {
        // Override parachute descent velocity to simulate ballistic fall
        system.simulated_vertical_velocity = -30.0f;
        system.vertical_acceleration = -FlightConfig::SIM_GRAVITY_MPS2;
        system.simulated_true_altitude += system.simulated_vertical_velocity * system.delta_time_seconds;
        if(isAtGround(system)) {
            system.simulated_true_altitude = FlightConfig::SIM_GROUND_ALTITUDE_METERS;
            system.simulated_vertical_velocity = 0.0f;
        }
    }

    updateMotorBurn(system);
    updateFlightPhase(system);
    simulateIMUAndPower(system);

    // Simulate GPS coordinates
    system.gps_latitude = 35.3331f + (system.simulation_step * 0.000001f);
    system.gps_longitude = -117.803f + (system.simulation_step * 0.000001f);

    // Simulate landing impact shock
    if (system.current_flight_phase == Flight_Landed && !system.landing_impact_detected)
    {
        // One-time shock detection when touching down
        if (system.previous_state == Descent || system.current_state == Descent) {
            system.landing_impact_detected = true;
        }
    }

    if (scenario == SCENARIO_SENSOR_FAILURE && system.simulated_true_altitude > 200.0f)
    {
        system.sensor_failure = true;
    }

    if (system.sensor_failure)
    {
        // Permanent flatline at 200m to trigger watchdog fault
        system.raw_altitude = 200.0f;
    }
    else
    {
        system.raw_altitude =
            system.simulated_true_altitude +
            simulatedNoiseSample(system);
    }

    system.simulation_step++;
}
