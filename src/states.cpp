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
    // ---------- State Entry ----------
    if (enterState(system))
    {
        std::cout << "\n[BOOT]\n";
        std::cout << "Initializing systems...\n";
        // Start boot timer
        system.boot_start_time =
            std::chrono::steady_clock::now();
        // Initialize ONCE
        system.IMU_Ready =
            initialize_IMU();
        system.Telemetry_Ready =
            initialize_telemetry();
        system.SD_Card_Ready =
            initialize_SD_card();
    }
    // ---------- Timeout Protection ----------
    const uint32_t BOOT_TIMEOUT_MS = 5000;
    long elapsed_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            system.current_time - system.boot_start_time
        ).count();
    if (elapsed_time > BOOT_TIMEOUT_MS)
    {
        raiseFault(system,
                   "BOOT timeout",
                   true);
        return;
    }
    // ---------- IMU Failure ----------
    if (!system.IMU_Ready)
    {
        raiseFault(system,
                   "IMU initialization failed",
                   true);
        return;
    }
    // ---------- Telemetry Failure ----------
    if (!system.Telemetry_Ready)
    {
        raiseFault(system,
                   "Telemetry initialization failed",
                   false); // Recoverable warning
    }
    // ---------- SD Card Failure ----------
    if (!system.SD_Card_Ready)
    {
        raiseFault(system,
                   "SD Card initialization failed",
                   false); // Recoverable warning
    }
    // ---------- Success Condition ----------
    if (system.IMU_Ready &&
        system.Telemetry_Ready &&
        system.SD_Card_Ready)
    {
        std::cout
            << "All systems initialized.\n";
        logEvent(system,
                 "BOOT: Systems initialized",
                 Event_System);
        system.current_state =
            TEST_MODE;
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

// Arms the flight computer only after every preflight condition has been
// satisfied continuously for PRELAUNCH_DEBOUNCE_SECONDS. This replaces the
// original time-only arming logic with evidence-based arming:
//
//   Critical checks (abort on first failure):
//     - No prior critical fault from boot
//     - IMU producing sane accelerometer values (~1g, not garbage)
//     - Battery voltage above minimum safe threshold
//     - Overall timeout not exceeded
//
//   Non-critical checks (reset debounce and warn, but do not abort):
//     - Rocket stationary (gyro quiet, accel stable at 1g)
//     - Rocket vertical (tilt within rail-alignment threshold)
//
//   Arming gate:
//     - All checks passing continuously for PRELAUNCH_DEBOUNCE_SECONDS
//
// FSM exit paths:
//   success  → Launch_Pad   (all conditions met, debounce satisfied)
//   critical → fault halt   (via raiseCriticalFault + main-loop fault guard)
void runPrelaunchCheckState(RocketSystem &system)
{
    // ---------- State Entry ----------
    if(enterState(system))
    {
        std::cout << "\n[PRELAUNCH_CHECK]\n";
        std::cout << "Checking launch conditions...\n";
        system.prelaunch_conditions_met_seconds = 0.0f;
    }

    // ---------- Hard Timeout (Abort Path) ----------
    // If the overall window expires without a successful arm, something is
    // wrong. Raise a critical fault rather than hanging in this state forever.
    if(stateElapsedSeconds(system) >=
       FlightConfig::PRELAUNCH_TIMEOUT_SECONDS)
    {
        raiseCriticalFault(
            system,
            "PRELAUNCH ABORT: conditions not met within timeout");
        return;
    }

    // ---------- Flaw 6 / 11: Critical Fault Gate ----------
    // If a prior state (e.g. BOOT) already raised a critical fault, refuse
    // to arm regardless of current sensor readings. Non-critical faults
    // (telemetry degraded, SD card warning) do NOT block arming.
    if(system.has_critical_fault)
    {
        raiseCriticalFault(
            system,
            "PRELAUNCH ABORT: critical fault already active");
        return;
    }

    // ---------- Flaw 2: IMU Sanity Check (Critical) ----------
    // The IMU must be producing physically plausible values.
    // A dead, railed, or glitching IMU is an immediate hard abort;
    // there is no safe flight without attitude data.
    if(!isIMUSane(system))
    {
        raiseCriticalFault(
            system,
            "PRELAUNCH ABORT: IMU sanity check failed");
        return;
    }

    // ---------- Flaw 3: Stationary Check (Non-Critical) ----------
    // Gyro quiet + accel stable at 1g. If the rocket is being carried
    // or disturbed, reset the debounce counter and warn. Do not abort;
    // the operator may be finishing setup and will stabilise shortly.
    if(!isStationary(system))
    {
        system.prelaunch_conditions_met_seconds = 0.0f;
        std::cout << "[PRELAUNCH] WARNING: rocket not stationary "
                     "-- debounce reset\n";
        return;
    }

    // ---------- Flaw 8: Verticality Check (Non-Critical) ----------
    // Tilt must be within the launch-rail alignment threshold.
    // A tilted rocket is dangerous but may be a temporary condition
    // (someone adjusting the rail), so reset debounce instead of aborting.
    if(!isVertical(system))
    {
        system.prelaunch_conditions_met_seconds = 0.0f;
        std::cout << "[PRELAUNCH] WARNING: tilt out of range ("
                  << system.tilt_angle_deg
                  << " deg) -- debounce reset\n";
        return;
    }

    // ---------- Flaw 5: Battery Check (Critical) ----------
    // Battery voltage must be above the minimum safe threshold.
    // A low battery mid-flight can cause an MCU brownout at the worst
    // possible moment (pyro firing). No negotiation.
    if(!isBatteryOk(system))
    {
        raiseCriticalFault(
            system,
            "PRELAUNCH ABORT: battery voltage below minimum safe level");
        return;
    }

    // ---------- Flaws 4 / 9: Debounce Accumulator ----------
    // All checks have passed this tick. Accumulate continuous good time.
    // Any single failing tick above resets the counter, so the rocket
    // must sustain a fully clean window before it is allowed to arm.
    system.prelaunch_conditions_met_seconds +=
        system.delta_time_seconds;

    std::cout << "[PRELAUNCH] All checks passing ("
              << system.prelaunch_conditions_met_seconds
              << " / "
              << FlightConfig::PRELAUNCH_DEBOUNCE_SECONDS
              << " s)\n";

    // ---------- Flaw 1 / 7: Evidence-Based Arming ----------
    // The system arms only when conditions have been continuously satisfied
    // for the full debounce window. Time alone is not sufficient;
    // evidence is required.
    if(system.prelaunch_conditions_met_seconds >=
       FlightConfig::PRELAUNCH_DEBOUNCE_SECONDS)
    {
        system.system_armed = true;

        std::cout << "System armed.\n";

        logEvent(system,
                 "PRELAUNCH_CHECK: System armed "
                 "(all conditions verified)",
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

    std::cout << "Altitude: "
              << readAltitude(system)
              << std::endl;

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

    std::cout << "Altitude: "
              << readAltitude(system)
              << std::endl;

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
        std::cout << "Rocket landed safely.\n";

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

        std::cout << system.mission_log
                  << std::endl;

        std::cout << "Mission Complete.\n";

        system.mission_complete = true;
    }
}
