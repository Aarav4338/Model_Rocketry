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

    if (scenario == SCENARIO_SENSOR_FAILURE && system.current_flight_phase == Flight_Coast)
    {
        // Flatline sensor during coast to test watchdog / altimeter faults
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
