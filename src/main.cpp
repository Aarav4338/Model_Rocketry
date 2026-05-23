#include <cstdio>
#include <chrono>
#include <iostream>
#include <thread>

#include "config.hpp"
#include "faults.hpp"
#include "filters.hpp"
#include "power.hpp"
#include "sensors.hpp"
#include "simulation.hpp"
#include "states.hpp"
#include "telemetry.hpp"
#include "timing.hpp"

// Builds the single shared mission object used by every subsystem. Keeping all
// mutable avionics and simulation state in one explicit structure makes the
// data flow easy to inspect and keeps the desktop simulator close to the style
// of a small embedded flight computer.
static RocketSystem createInitialSystem()
{
    RocketSystem system = {};

    system.current_state = BOOT;
    system.previous_state = Beacon;
    system.current_flight_phase = Flight_Grounded;

    system.system_armed = false;
    system.descending = false;

    system.fault_detected = false;
    system.sensor_failure = false;
    system.watchdog_enabled = true;

    system.payload_deployed = false;
    system.mission_complete = false;
    system.thrust_active = false;
    system.burnout_detected = false;

    system.imu_powered = true;
    system.high_rate_logging_enabled = true;
    system.flight_telemetry_enabled = true;
    system.recovery_beacon_enabled = false;
    system.landed_power_saving_applied = false;

    system.raw_altitude = 0.0f;
    system.filtered_altitude = 0.0f;
    system.previous_altitude = 0.0f;
    system.vertical_velocity = 0.0f;
    system.vertical_acceleration = 0.0f;
    system.last_filtered_altitude_for_velocity = 0.0f;
    system.simulated_true_altitude = 0.0f;
    system.simulated_vertical_velocity = 0.0f;
    system.motor_burn_time_remaining = 0.0f;
    system.launch_reference_altitude = 0.0f;
    system.landing_stationary_time_seconds = 0.0f;

    system.simulation_step = 0;
    system.telemetry_sequence = 0;

    system.last_telemetry_time_seconds = 0.0f;

    system.error_message[0] = '\0';
    system.mission_log[0] = '\0';
    system.mission_log_length = 0;
    system.mission_event_count = 0;

    initializeTiming(system);

    return system;
}

// Routes the current FSM state to its handler. The switch is intentionally
// plain and explicit so the ten mission states remain visible and stable while
// other subsystems evolve underneath them.
static void dispatchState(RocketSystem &system)
{
    switch(system.current_state)
    {
        case BOOT:
            runBootState(system);
            break;

        case TEST_MODE:
            runTestModeState(system);
            break;

        case Prelaunch_Check:
            runPrelaunchCheckState(system);
            break;

        case Launch_Pad:
            runLaunchPadState(system);
            break;

        case Ascent:
            runAscentState(system);
            break;

        case Apogee_confirm:
            runApogeeConfirmState(system);
            break;

        case Payload_Separation:
            runPayloadSeparationState(system);
            break;

        case Descent:
            runDescentState(system);
            break;

        case Landed:
            runLandedState(system);
            break;

        case Beacon:
            runBeaconState(system);
            break;

        default:
            std::cout << "Unknown state.\n";
            system.mission_complete = true;
            break;
    }
}

#include <cstdlib>
#include <ctime>

void runSimulation(SimulationScenario scenario, const char* scenario_name)
{
    std::cout << "\n========================================\n";
    std::cout << "RUNNING SCENARIO: " << scenario_name << "\n";
    std::cout << "========================================\n";

    RocketSystem system = createInitialSystem();

    while(!system.mission_complete)
    {
        updateTiming(system);

        if(system.fault_detected)
        {
            std::cout << "\n[FAULT DETECTED]\n";
            std::cout << system.error_message
                      << std::endl;

            // In a real system, the rocket might try to recover or stop.
            // For the simulation, we'll fast-forward the physics to ground impact
            // to show the end result, or just break depending on the fault.
            // But the FSM states like descent will handle landing. 
            // If the fault is terminal, break:
            if(system.current_flight_phase == Flight_Grounded) {
                break;
            }
        }

        updateSimulation(system, scenario);
        updateSensors(system);
        filterAltitude(system);
        updateVelocity(system);

        if(shouldSendTelemetry(system))
        {
            sendTelemetry(system);
        }

        dispatchState(system);
        checkWatchdog(system);

        if(FlightConfig::MAIN_LOOP_SLEEP_MILLISECONDS > 0)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    FlightConfig::MAIN_LOOP_SLEEP_MILLISECONDS));
        }

        // Failsafe break to avoid infinite loops if something goes horribly wrong
        if(system.mission_elapsed_seconds > 300.0f) {
            std::cout << "Simulation timeout limit reached.\n";
            break;
        }
    }
}

// Main avionics loop wrapper. Runs the desktop simulation across multiple 
// scenarios (success, sensor failure, etc.) for testing.
int main()
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    runSimulation(SCENARIO_SUCCESS, "Nominal Flight (Success)");
    runSimulation(SCENARIO_MOTOR_FAILURE, "Motor Thrust Failure (Early Burnout)");
    runSimulation(SCENARIO_SENSOR_FAILURE, "Altimeter Sensor Failure (Flatline)");
    runSimulation(SCENARIO_PARACHUTE_FAILURE, "Parachute Deployment Failure (Ballistic)");

    return 0;
}
