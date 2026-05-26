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

// Main avionics loop. Each cycle updates time, advances the physics simulator,
// filters sensor-like data, derives velocity, emits telemetry, runs the FSM, and
// finally performs independent watchdog supervision.
int main()
{
    RocketSystem system =
        createInitialSystem();

    while(!system.mission_complete)
    {
        updateTiming(system);

        if(system.fault_detected)
        {
            std::cout << "\n[FAULT DETECTED]\n";
            std::cout << system.error_message
                      << std::endl;

            break;
        }

        updateSimulation(system);
        filterAltitude(system);
        updateIMUReadings(system);
        updateVelocity(system);

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
    }

    return 0;
}
