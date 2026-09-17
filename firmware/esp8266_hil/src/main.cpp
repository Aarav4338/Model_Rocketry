// ESP8266 bench firmware entry point.
//
// This is the Arduino twin of the desktop src/main.cpp: same tick order,
// same FSM, same fault/telemetry/deployment logic -- just a setup()/loop()
// driver instead of a command-line while() loop, because there's no argv[]
// or Sleep() on a microcontroller.
//
// In FlightConfig::HIL_MODE (config.hpp, default true), this runs the exact
// same fabricated flight the desktop `./rocket-avionics <scenario>` runs
// (see FlightConfig::HIL_SCENARIO for which one), except now the FSM's
// decisions drive REAL hardware: a real LoRa radio transmits every
// telemetry packet, a real servo fires on deployment, a real EEPROM sector
// records crash-recovery state, and the ESP8266's real watchdog is fed each
// tick. The IMU/barometer/GPS themselves can sit still on the bench --
// hal_esp8266.cpp substitutes physics-model values for their readings while
// HIL_MODE is on. Flip HIL_MODE to false once you want the real, physically
// stationary (or later, actually flying) sensors driving the FSM instead --
// that path is src/real_sensors.cpp.

#include <Arduino.h>

#include "config.hpp"
#include "faults.hpp"
#include "filters.hpp"
#include "hal.hpp"
#include "hil_state.hpp"
#include "logging.hpp"
#include "mission_io.hpp"
#include "power.hpp"
#include "real_sensors.hpp"
#include "sensors.hpp"
#include "simulation.hpp"
#include "states.hpp"
#include "telemetry.hpp"
#include "timing.hpp"

// Declared in hal_esp8266.cpp -- attaches the deployment servo. Not part of
// the Hardware:: namespace because hal.hpp has no init entry point for an
// actuator (the desktop build has no actuator hardware to initialize).
void hilAttachDeploymentServo();

extern bool loadSystemState(RocketSystem &system);
extern bool saveSystemState(const RocketSystem &system);

// src/simulation.cpp's SCENARIO_MCU_RESET branch does
// `extern bool simulated_crash_triggered;` and expects some main.cpp to
// define it (the desktop build's main.cpp does). This is that definition
// for the Arduino build -- without it, simulation.cpp still compiles (it's
// only a declaration there) but linking fails.
bool simulated_crash_triggered = false;

static RocketSystem g_system;

// Mirrors createInitialSystem() in the desktop src/main.cpp. Kept as its own
// function for the same reason: one place to see every field the FSM
// depends on at cold boot.
static void createInitialSystem(RocketSystem &system)
{
    system = {};

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

    system.imu_accel_magnitude = 0.0f;
    system.imu_gyro_rate = 0.0f;
    system.tilt_angle_deg = 0.0f;
    system.battery_voltage = 0.0f;

    system.prelaunch_conditions_met_seconds = 0.0f;
    system.has_critical_fault = false;

    system.launch_pad_altitude_accumulator = 0.0f;
    system.launch_pad_altitude_sample_count = 0;
    system.launch_detection_seconds = 0.0f;

    system.consecutive_descent_ticks = 0;
    system.apogee_debounce_seconds = 0.0f;
    system.peak_altitude_m = 0.0f;

    system.apogee_climb_debounce_seconds = 0.0f;
    system.deployment_attempts = 0;
    system.last_deployment_attempt_time = 0.0f;
    system.landing_impact_detected = false;
    system.ballistic_descent_warning_issued = false;
    system.flight_data_saved = false;
    system.beacon_power_mode = 0;
    system.gps_latitude = 0.0f;
    system.gps_longitude = 0.0f;
    system.accel_x = 0.0f;
    system.accel_y = 0.0f;
    system.accel_z = 0.0f;
    system.roll = 0.0f;
    system.pitch = 0.0f;
    system.yaw = 0.0f;

    system.simulation_step = 0;
    system.telemetry_sequence = 0;

    system.last_telemetry_time_seconds = 0.0f;

    system.error_message[0] = '\0';
    system.mission_event_count = 0;

    initializeTiming(system);

    if (loadSystemState(system) &&
        system.current_state > Launch_Pad &&
        system.current_state < Landed)
    {
        MISSION_COUT << "[RECOVERY] Previous flight state found! Resuming from state: "
                     << static_cast<int>(system.current_state) << "\n";
        logEvent(system, "SYSTEM REBOOT: Recovered inflight state from NVRAM", Event_System);
    }
    else
    {
        MISSION_COUT << "[BOOT] Cold start.\n";
    }
}

// Mirrors dispatchState() in the desktop src/main.cpp.
static void dispatchState(RocketSystem &system)
{
    switch (system.current_state)
    {
        case BOOT:               runBootState(system); break;
        case TEST_MODE:          runTestModeState(system); break;
        case Prelaunch_Check:    runPrelaunchCheckState(system); break;
        case Launch_Pad:         runLaunchPadState(system); break;
        case Ascent:             runAscentState(system); break;
        case Apogee_confirm:     runApogeeConfirmState(system); break;
        case Payload_Separation: runPayloadSeparationState(system); break;
        case Descent:            runDescentState(system); break;
        case Landed:             runLandedState(system); break;
        case Beacon:             runBeaconState(system); break;
        default:
            MISSION_COUT << "Unknown state.\n";
            system.mission_complete = true;
            break;
    }
}

static SimulationScenario activeScenario()
{
    switch (FlightConfig::HIL_SCENARIO)
    {
        case 1: return SCENARIO_MOTOR_FAILURE;
        case 2: return SCENARIO_SENSOR_FAILURE;
        case 3: return SCENARIO_PARACHUTE_FAILURE;
        case 4: return SCENARIO_MCU_RESET;
        default: return SCENARIO_SUCCESS;
    }
}

void setup()
{
    Hardware::initMCU();
    Hardware::initWatchdog(0);

    Hardware::initI2C();
    Hardware::initSPI();
    Hardware::initUART();

    Hardware::initPrimaryIMU();
    Hardware::initRedundantAltimeter();
    Hardware::initGNSS();
    Hardware::initRadio();
    Hardware::initSDCard();
    Hardware::initAmbientSensor();

    hilAttachDeploymentServo();

    MISSION_COUT << "\n========================================\n";
    MISSION_COUT << (FlightConfig::HIL_MODE
                          ? "HIL BENCH TEST -- fabricated flight over real electronics\n"
                          : "REAL SENSOR MODE -- flying on actual sensor input\n");
    MISSION_COUT << "========================================\n";

    createInitialSystem(g_system);
}

void loop()
{
    if (g_system.mission_complete)
    {
        // Bench-testing convenience: rather than needing a physical reset
        // to try the FSM again, auto-restart the mission a few seconds
        // after it completes. Comment this out if you'd rather it just
        // halt (e.g. once you're testing on an actual flight-bound board).
        static uint32_t completed_at_ms = 0;
        if (completed_at_ms == 0) completed_at_ms = millis();
        Hardware::resetWatchdog();
        if (millis() - completed_at_ms > 8000)
        {
            completed_at_ms = 0;
            MISSION_COUT << "\n--- RESTARTING MISSION FOR NEXT BENCH RUN ---\n";
            simulated_crash_triggered = false; // see the reset_test note below
            createInitialSystem(g_system);
        }
        delay(50);
        return;
    }

    updateTiming(g_system);

    if (g_system.fault_detected)
    {
        MISSION_COUT << "\n[FAULT DETECTED]\n";
        MISSION_COUT << g_system.error_message << "\n";
    }

    if (FlightConfig::HIL_MODE)
    {
        updateSimulation(g_system, activeScenario());

        // Feed the fabricated truth into the HAL's sensor functions so
        // telemetry (pressure/temp/IMU/GNSS/battery) stays numerically
        // consistent with the flight the FSM is reacting to.
        g_hil.true_altitude = g_system.simulated_true_altitude;
        g_hil.vertical_velocity = g_system.simulated_vertical_velocity;
        g_hil.accel_magnitude = g_system.imu_accel_magnitude;
        g_hil.battery_voltage = g_system.battery_voltage;
    }
    else
    {
        updateRealSensorInputs(g_system);
    }

    updateSensors(g_system);
    filterAltitude(g_system);
    updateIMUReadings(g_system);
    updateVelocity(g_system);

    receiveRFCommands(g_system);

    if (shouldSendTelemetry(g_system))
    {
        sendTelemetry(g_system);
    }

    dispatchState(g_system);
    checkWatchdog(g_system);
    Hardware::resetWatchdog();

    // SCENARIO_MCU_RESET (HIL_SCENARIO=4): simulation.cpp sets
    // simulated_crash_triggered when the injected flight decides to crash
    // mid-ascent, but only THIS loop actually acts on it -- mirroring the
    // desktop build's runSimulation(), which checks the flag after every
    // tick and calls Hardware::systemReset(). On the ESP8266,
    // Hardware::systemReset() calls ESP.restart(), a REAL reboot: setup()
    // runs again from scratch, every global (including
    // simulated_crash_triggered itself) goes back to its initial value, and
    // only the EEPROM-backed crash-recovery blob survives -- so the
    // recovery you see after this is the genuine crash-recovery path, not a
    // simulated one.
    if (simulated_crash_triggered && g_system.current_state == Ascent)
    {
        MISSION_COUT << "\n[SIMULATION] <<< CRITICAL HARDWARE RESET INJECTED >>>\n";
        Hardware::systemReset();
        return; // unreachable after ESP.restart(), kept for clarity
    }

    if (FlightConfig::MAIN_LOOP_SLEEP_MILLISECONDS > 0)
    {
        // Unlike the desktop build's Sleep() call, delay() here still lets
        // the ESP8266 core service its own SDK housekeeping via yield(), so
        // this does not by itself risk a watchdog reset.
        delay(static_cast<unsigned long>(FlightConfig::MAIN_LOOP_SLEEP_MILLISECONDS));
    }
}
