#include <cstdio>
#include <chrono>
#include <iostream>
#include <windows.h>

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
    system.parachute_failure_detected = false;

    system.imu_powered = true;
    system.high_rate_logging_enabled = true;
    system.flight_telemetry_enabled = true;
    system.recovery_beacon_enabled = false;
    system.landed_power_saving_applied = false;

    // RF command receiver — starts unmuted, no pending command
    system.telemetry_muted = false;
    system.last_rf_command[0] = '\0';

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
    system.max_descent_velocity = 0.0f;

    // IMU / power stub fields — zeroed here; simulation fills them
    // each tick before the FSM and prelaunch checks run.
    system.imu_accel_magnitude = 0.0f;
    system.imu_gyro_rate       = 0.0f;
    system.tilt_angle_deg      = 0.0f;
    system.battery_voltage     = 0.0f;

    // Prelaunch debounce accumulator and critical-fault flag.
    system.prelaunch_conditions_met_seconds = 0.0f;
    system.has_critical_fault               = false;

    // Launch Pad state fields — reset here; state entry resets them again.
    system.launch_pad_altitude_accumulator  = 0.0f;
    system.launch_pad_altitude_sample_count = 0;
    system.launch_detection_seconds         = 0.0f;

    // Ascent state fields — reset here; state entry resets them again.
    system.consecutive_descent_ticks = 0;
    system.apogee_debounce_seconds   = 0.0f;
    system.peak_altitude_m           = 0.0f;

    // Post-ascent state fields
    system.apogee_climb_debounce_seconds = 0.0f;
    system.deployment_attempts = 0;
    system.last_deployment_attempt_time = 0.0f;
    system.landing_impact_detected = false;
    system.ballistic_descent_warning_issued = false;
    system.flight_data_saved = false;
    system.beacon_power_mode = 0;
    system.gps_latitude = 0.0f;
    system.gps_longitude = 0.0f;


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
        updateIMUReadings(system);
        updateVelocity(system);

        // POINT 4: Check for incoming RF commands before deciding to transmit
        receiveRFCommands(system);

        if(shouldSendTelemetry(system))
        {
            sendTelemetry(system);
        }

        dispatchState(system);
        checkWatchdog(system);

        if(FlightConfig::MAIN_LOOP_SLEEP_MILLISECONDS > 0)
        {
            // MinGW.org GCC (Win32 thread model) does not expose
            // std::this_thread, so we use the Windows Sleep() API directly.
            // Semantics are identical: argument is milliseconds.
            Sleep(static_cast<DWORD>(
                FlightConfig::MAIN_LOOP_SLEEP_MILLISECONDS));
        }

        // Failsafe break to avoid infinite loops if something goes horribly wrong
        if(system.mission_elapsed_seconds > 700.0f) {
            std::cout << "Simulation timeout limit reached.\n";
            break;
        }
    }
}

#include <cstring>

// Main avionics loop wrapper. Runs the desktop simulation based on the
// scenario requested via command-line arguments.
int main(int argc, char* argv[])
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    if (argc < 2) {
        std::cout << "Usage: ./rocket-avionics <scenario>\n";
        std::cout << "Available scenarios:\n";
        std::cout << "  success    : Nominal Flight\n";
        std::cout << "  motor      : Motor Thrust Failure (Early Burnout)\n";
        std::cout << "  sensor     : Altimeter Sensor Failure (Flatline)\n";
        std::cout << "  parachute  : Parachute Deployment Failure (Ballistic)\n";
        std::cout << "  all        : Run all 4 scenarios sequentially\n";
        return 1;
    }

    const char* arg = argv[1];

    if (std::strcmp(arg, "success") == 0) {
        runSimulation(SCENARIO_SUCCESS, "Nominal Flight (Success)");
    } else if (std::strcmp(arg, "motor") == 0) {
        runSimulation(SCENARIO_MOTOR_FAILURE, "Motor Thrust Failure (Early Burnout)");
    } else if (std::strcmp(arg, "sensor") == 0) {
        runSimulation(SCENARIO_SENSOR_FAILURE, "Altimeter Sensor Failure (Flatline)");
    } else if (std::strcmp(arg, "parachute") == 0) {
        runSimulation(SCENARIO_PARACHUTE_FAILURE, "Parachute Deployment Failure (Ballistic)");
    } else if (std::strcmp(arg, "all") == 0) {
        runSimulation(SCENARIO_SUCCESS, "Nominal Flight (Success)");
        runSimulation(SCENARIO_MOTOR_FAILURE, "Motor Thrust Failure (Early Burnout)");
        runSimulation(SCENARIO_SENSOR_FAILURE, "Altimeter Sensor Failure (Flatline)");
        runSimulation(SCENARIO_PARACHUTE_FAILURE, "Parachute Deployment Failure (Ballistic)");
    } else {
        std::cout << "Unknown scenario: " << arg << "\n";
        return 1;
    }

    return 0;
}
