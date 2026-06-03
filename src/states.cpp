#include "states.hpp"

#include <iostream>

#include "config.hpp"
#include "deployment.hpp"
#include "faults.hpp"
#include "logging.hpp"
#include "power.hpp"
#include "sensors.hpp"
#include "timing.hpp"

// Detects first entry into each FSM state. Entry handling is centralized so all
// states update `previous_state` and `state_entry_time` consistently.
static bool enterState(RocketSystem &system)
{
    if(system.current_state == system.previous_state)
    {
        return false;
    }

    system.previous_state =
        system.current_state;

    markStateEntry(system);

    return true;
}

// Boots the avionics abstractions. In the desktop build these initializers are
// stubs, but the state models the embedded startup sequence that would verify
// IMU, telemetry, and storage readiness before mission logic continues.
void runBootState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[BOOT]\n";
        std::cout << "Initializing systems...\n";
    }

    bool IMU_Ready =
        initialize_IMU();

    bool Redundant_Altimeter_Ready =
        initialize_redundant_altimeter();

    if(!IMU_Ready || !Redundant_Altimeter_Ready)
    {
        raiseFault(system,
                   "Sensor initialization failed",
                   true);

        return;
    }

    bool Telemetry_Ready =
        initialize_telemetry();

    bool SD_Card_Ready =
        initialize_SD_card();

    if(IMU_Ready &&
       Redundant_Altimeter_Ready &&
       Telemetry_Ready &&
       SD_Card_Ready)
    {
        std::cout << "All systems initialized.\n";

        logEvent(system,
                 "BOOT: Systems initialized",
                 Event_System);

        system.current_state = TEST_MODE;
    }
}

// Represents ground diagnostics. This state is deliberately time-gated because
// diagnostics are a preflight operation rather than a flight event detected from
// sensors.
void runTestModeState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[TEST_MODE]\n";
        std::cout << "Running diagnostics...\n";
    }

    if(stateElapsedSeconds(system) >=
       FlightConfig::TEST_MODE_DURATION_SECONDS)
    {
        std::cout << "Diagnostics complete.\n";

        logEvent(system,
                 "TEST_MODE: Diagnostics complete",
                 Event_State);

        system.current_state =
            Prelaunch_Check;
    }
}

// Arms the flight computer after prelaunch checks. Arming only allows launch
// detection; it does not itself prove the rocket has moved.
void runPrelaunchCheckState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[PRELAUNCH_CHECK]\n";
        std::cout << "Checking launch conditions...\n";
    }

    if(stateElapsedSeconds(system) >=
       FlightConfig::PRELAUNCH_CHECK_DURATION_SECONDS)
    {
        system.system_armed = true;

        std::cout << "System armed.\n";

        logEvent(system,
                 "PRELAUNCH_CHECK: System armed",
                 Event_State);

        system.current_state =
            Launch_Pad;
    }
}

// Waits on the pad for filtered flight evidence. The simulator may ignite the
// motor after a configured delay, but this state enters `Ascent` only when
// processed altitude or derived velocity shows launch motion.
void runLaunchPadState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[LAUNCH_PAD]\n";
        std::cout << "Waiting for launch...\n";

        system.launch_reference_altitude =
            readAltitude(system);
    }

    if(canEnterAscent(system))
    {
        std::cout << "Launch detected!\n";

        logEvent(system,
                 "LAUNCH DETECTED",
                 Event_Flight);

        system.current_state =
            Ascent;
    }
}

// Monitors powered ascent and coast using filtered altitude and derived
// vertical velocity. Apogee is treated as a candidate when velocity naturally
// crosses zero; no hardcoded altitude target is used.
void runAscentState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[ASCENT]\n";
    }

    std::cout << "Time: " << system.mission_elapsed_seconds << "s | Altitude: "
              << readAltitude(system)
              << " m" << std::endl;

    if(canConfirmApogee(system))
    {
        if(hasPassedApogee(system))
        {
            std::cout << "Apogee detected.\n";

            logEvent(system,
                     "APOGEE DETECTED: vertical velocity crossed zero",
                     Event_Flight);

            system.current_state =
                Apogee_confirm;
        }
    }

    system.previous_altitude =
        readAltitude(system);
}

// Confirms that the apogee candidate is real. The state actively rejects resumed
// upward motion before deployment so one noisy or transient velocity sample
// cannot command payload separation.
void runApogeeConfirmState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[APOGEE_CONFIRM]\n";
        std::cout << "Confirming apogee...\n";
    }

    if(hasResumedClimb(system))
    {
        std::cout << "Apogee rejected; climb resumed.\n";

        logEvent(system,
                 "APOGEE REJECTED: climb resumed",
                 Event_Flight);

        system.current_state =
            Ascent;

        return;
    }

    if(stateElapsedMilliseconds(system) >=
       FlightConfig::APOGEE_CONFIRM_DURATION_MILLISECONDS &&
       hasPassedApogee(system))
    {
        logEvent(system,
                 "APOGEE CONFIRMED: deployment authorized",
                 Event_Flight);

        system.current_state =
            Payload_Separation;
    }
}

// Requests payload deployment through the deployment subsystem. Hardware
// details stay behind `triggerPayloadDeployment()` so the FSM does not care
// whether future hardware uses GPIO, a servo, or a pyro-safe driver.
void runPayloadSeparationState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[PAYLOAD_SEPARATION]\n";
        std::cout << "Deploying payload...\n";

        triggerPayloadDeployment(system);
    }

    if(stateElapsedSeconds(system) >=
       FlightConfig::PAYLOAD_SEPARATION_DURATION_SECONDS &&
       canEnterDescent(system))
    {
        system.current_state =
            Descent;
    }
}

// Tracks the falling vehicle after deployment. Landing is detected by the
// filter layer's sustained near-zero velocity accumulator rather than exact
// altitude equality.
void runDescentState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[DESCENT]\n";

        logEvent(system,
                 "DESCENT CONFIRMED",
                 Event_Flight);
    }

    std::cout << "Time: " << system.mission_elapsed_seconds << "s | Altitude: "
              << readAltitude(system)
              << " m" << std::endl;

    if (!system.parachute_failure_detected && system.vertical_velocity < -10.0f) {
        system.parachute_failure_detected = true;
        std::cout << "Parachute failure detected!\n";
        logEvent(system, "PARACHUTE FAILURE DETECTED: High descent velocity", Event_Fault);
    }

    if(hasDetectedLanding(system))
    {
        logEvent(system,
                 "LANDING DETECTED",
                 Event_Flight);

        system.current_state =
            Landed;
    }
}

// Handles post-landing shutdown of high-rate flight systems. The power subsystem
// owns the actual actions so this mission state stays hardware independent.
void runLandedState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[LANDED]\n";
        
        if (system.max_descent_velocity < -10.0f) {
            std::cout << "CRASH DETECTED: High-velocity impact (" << system.max_descent_velocity << " m/s).\n";
            logEvent(system, "CRASH DETECTED: High-velocity impact", Event_Fault);
        } else {
            std::cout << "Rocket landed safely.\n";
            logEvent(system, "LANDED SAFELY", Event_Flight);
        }

        powerDownLandedSystems(system);
    }

    if(stateElapsedSeconds(system) >=
       FlightConfig::LANDED_DURATION_SECONDS)
    {
        system.current_state =
            Beacon;
    }
}

// Enables recovery signaling after the vehicle is landed and flight systems are
// reduced. The beacon phase prints the accumulated mission log for desktop
// review.
void runBeaconState(RocketSystem &system)
{
    if(enterState(system))
    {
        std::cout << "\n[BEACON]\n";
        std::cout << "Beacon active.\n";

        enableRecoveryBeacon(system);
    }

    if(stateElapsedSeconds(system) >=
       FlightConfig::BEACON_DURATION_SECONDS)
    {
        std::cout << "\n=== MISSION LOG ===\n";

        std::cout << "(Mission log has been saved to SD Card)\n";

        std::cout << "Mission Complete.\n";

        system.mission_complete = true;
    }
}
