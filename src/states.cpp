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
        // Start boot timer using the current mission time reference.
        system.boot_start_time = system.current_time;
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
    uint32_t elapsed_time =
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

// Waits on the pad for confirmed, sustained, multi-sensor flight evidence.
// The state is hardened against six classes of false positive / safety gaps:
//
//   Flaw 5 — Arming verification:
//     On entry the system confirms system_armed is set. If somehow the FSM
//     reaches this state without arming, a critical fault is raised immediately.
//
//   Flaw 4 — 30-minute launch timeout:
//     If no launch is detected within LAUNCH_PAD_TIMEOUT_SECONDS the mission
//     is considered aborted and a critical fault halts the system safely.
//
//   Flaw 6 — Post-entry inhibit window:
//     Launch detection is suppressed for LAUNCH_PAD_INHIBIT_SECONDS after
//     state entry. This absorbs bumps from rail placement and residual
//     vibration immediately after the arming transition.
//
//   Flaw 1 — Averaged altitude reference:
//     During the inhibit window the state collects LAUNCH_PAD_ALTITUDE_AVG_SAMPLES
//     altitude readings and averages them. The mean is far more resistant to
//     a single barometric noise spike than one instantaneous reading.
//
//   Flaw 3 — Multi-sensor AND confirmation:
//     canEnterAscent() delegates to hasDetectedLaunch(), which now requires
//     BOTH vertical velocity AND altitude rise to agree (AND fusion, not OR).
//     A single IMU bump raises velocity but not altitude; a baro glitch raises
//     altitude but not velocity. Both channels must fire together.
//
//   Flaw 2 — Debounced detection:
//     All launch criteria must remain satisfied continuously for
//     LAUNCH_PAD_DEBOUNCE_SECONDS before ascent is confirmed. A single
//     passing tick is never enough; any failing tick resets the accumulator.
void runLaunchPadState(RocketSystem &system)
{
    // ---------- Flaw 5: Arming Verification (State Entry) ----------
    // The FSM should never reach Launch_Pad without system_armed being true.
    // If it does, something went badly wrong in Prelaunch_Check; abort now.
    if(enterState(system))
    {
        std::cout << "\n[LAUNCH_PAD]\n";

        if(!system.system_armed)
        {
            raiseCriticalFault(
                system,
                "LAUNCH_PAD: entered without system armed");
            return;
        }

        std::cout << "Waiting for launch...\n";
        std::cout << "[LAUNCH_PAD] Inhibit window active ("
                  << FlightConfig::LAUNCH_PAD_INHIBIT_SECONDS
                  << " s) -- collecting altitude baseline...\n";

        // Reset averaging and debounce accumulators on every fresh entry.
        system.launch_pad_altitude_accumulator  = 0.0f;
        system.launch_pad_altitude_sample_count = 0;
        system.launch_detection_seconds         = 0.0f;
    }

    // ---------- Flaw 4: 30-Minute Launch Timeout ----------
    // If no launch is detected within the allowed window, raise a critical
    // fault. This handles: ignition failure, operator abort, dead igniter.
    if(stateElapsedSeconds(system) >=
       FlightConfig::LAUNCH_PAD_TIMEOUT_SECONDS)
    {
        raiseCriticalFault(
            system,
            "LAUNCH_PAD ABORT: no launch detected within 30-minute window");
        return;
    }

    // ---------- Flaws 1 + 6: Inhibit Window + Altitude Averaging ----------
    // For the first LAUNCH_PAD_INHIBIT_SECONDS after entry:
    //   - All launch detection is suppressed (protects against rail bumps).
    //   - Altitude samples are collected to build a stable reference mean.
    // Once the inhibit window expires the averaged reference is locked in
    // and detection begins on the next tick.
    if(stateElapsedSeconds(system) <
       FlightConfig::LAUNCH_PAD_INHIBIT_SECONDS)
    {
        // Accumulate samples toward the averaged baseline.
        if(system.launch_pad_altitude_sample_count <
           FlightConfig::LAUNCH_PAD_ALTITUDE_AVG_SAMPLES)
        {
            system.launch_pad_altitude_accumulator +=
                readAltitude(system);

            system.launch_pad_altitude_sample_count++;

            // Once we have the target sample count, compute and lock the mean.
            if(system.launch_pad_altitude_sample_count ==
               FlightConfig::LAUNCH_PAD_ALTITUDE_AVG_SAMPLES)
            {
                system.launch_reference_altitude =
                    system.launch_pad_altitude_accumulator /
                    static_cast<float>(
                        FlightConfig::LAUNCH_PAD_ALTITUDE_AVG_SAMPLES);

                std::cout << "[LAUNCH_PAD] Altitude baseline locked: "
                          << system.launch_reference_altitude
                          << " m (avg of "
                          << FlightConfig::LAUNCH_PAD_ALTITUDE_AVG_SAMPLES
                          << " samples)\n";
            }
        }

        // Detection suppressed — still inside inhibit window.
        return;
    }

    // ---------- Flaws 2 + 3: Debounced Multi-Sensor Launch Detection ----------
    // canEnterAscent() checks system_armed AND hasDetectedLaunch().
    // hasDetectedLaunch() now requires velocity AND altitude to both confirm
    // (AND fusion — single-sensor spikes cannot trigger a false launch).
    // The result must remain true for LAUNCH_PAD_DEBOUNCE_SECONDS continuously.
    if(canEnterAscent(system))
    {
        system.launch_detection_seconds +=
            system.delta_time_seconds;

        if(system.launch_detection_seconds >=
           FlightConfig::LAUNCH_PAD_DEBOUNCE_SECONDS)
        {
            std::cout << "Launch detected!\n";

            logEvent(system,
                     "LAUNCH DETECTED",
                     Event_Flight);

            system.current_state =
                Ascent;
        }
    }
    else
    {
        // Any failing tick resets the counter — launch evidence must be
        // sustained, not just momentarily present.
        if(system.launch_detection_seconds > 0.0f)
        {
            std::cout << "[LAUNCH_PAD] Launch evidence lost — "
                         "debounce reset\n";
        }

        system.launch_detection_seconds = 0.0f;
    }
}

// Monitors powered ascent and coast using filtered altitude, derived vertical
// velocity, and IMU acceleration. Apogee transition is hardened against eight
// classes of false trigger and safety gaps:
//
//   Flaw 8 — Entry log:
//     logEvent records ASCENT ENTERED for a clean post-flight timeline.
//
//   Flaw 7 — Single altitude sample per tick:
//     readAltitude() is called once into a local variable; all uses within
//     the tick read from that local, eliminating duplicate hardware calls
//     and ensuring a deterministic, consistent value.
//
//   Flaw 4 — Ascent inhibit window:
//     Apogee detection is fully suppressed for ASCENT_APOGEE_INHIBIT_SECONDS
//     after state entry. This absorbs burnout vibration and filter
//     transients that can produce a momentary velocity dip at ignition end.
//
//   Flaw 3 — Altitude floor gate:
//     peak_altitude_m is tracked every tick. Apogee logic is suppressed
//     unless the rocket has risen at least ASCENT_MIN_APOGEE_ALTITUDE_M
//     above the launch reference. A 2-metre wobble on the rail cannot
//     deploy the parachute.
//
//   Flaw 5 — Three-channel sensor fusion:
//     Apogee is a candidate only when three independent signals agree:
//       1. isDescending() — N consecutive ticks of negative velocity
//       2. hasPassedApogee() — velocity below hysteresis threshold
//       3. isNearFreefall() — IMU accel near 1g (engine definitely off)
//     Any single-channel glitch cannot satisfy all three.
//
//   Flaw 2 — Hardened isDescending():
//     Now backed by consecutive_descent_ticks (maintained in filters.cpp)
//     instead of a single-tick boolean flag.
//
//   Flaw 6 — Apogee velocity hysteresis:
//     VELOCITY_APOGEE_THRESHOLD_MPS is now -2.0 m/s (was 0.0), so the
//     rocket must be clearly descending, not oscillating around zero.
//
//   Flaw 1 — Apogee debounce:
//     All three conditions must hold continuously for
//     ASCENT_APOGEE_DEBOUNCE_SECONDS before transitioning to Apogee_confirm.
//     Any failing tick resets the accumulator.
void runAscentState(RocketSystem &system)
{
    // ---------- Flaw 8: Entry Log ----------
    if(enterState(system))
    {
        std::cout << "\n[ASCENT]\n";

        logEvent(system,
                 "ASCENT ENTERED",
                 Event_Flight);

        // Reset ascent-specific accumulators on every fresh entry.
        system.apogee_debounce_seconds   = 0.0f;
        system.peak_altitude_m           = system.launch_reference_altitude;
        system.consecutive_descent_ticks = 0;
    }

    // ---------- Flaw 7: Single Altitude Read ----------
    // Sample once; every altitude use this tick reads from this local.
    const float altitude = readAltitude(system);

    std::cout << "Altitude: " << altitude << std::endl;

    // Track the highest altitude reached during this ascent.
    // Used for the altitude floor check below.
    if(altitude > system.peak_altitude_m)
    {
        system.peak_altitude_m = altitude;
    }

    // ---------- Flaw 4: Ascent Inhibit Window ----------
    // Suppress apogee detection for the first ASCENT_APOGEE_INHIBIT_SECONDS
    // after state entry. Burnout vibration and filter warm-up can produce
    // a brief velocity dip immediately after launch confirmation.
    if(stateElapsedSeconds(system) <
       FlightConfig::ASCENT_APOGEE_INHIBIT_SECONDS)
    {
        system.previous_altitude = altitude;
        return;
    }

    // ---------- Flaw 3: Altitude Floor Gate ----------
    // Apogee detection is meaningless below the minimum safe altitude.
    // If the rocket never climbed far enough, keep waiting.
    const float relative_altitude =
        altitude - system.launch_reference_altitude;

    if(relative_altitude < FlightConfig::ASCENT_MIN_APOGEE_ALTITUDE_M)
    {
        system.apogee_debounce_seconds = 0.0f;
        system.previous_altitude       = altitude;
        return;
    }

    // ---------- Flaws 1 + 2 + 5 + 6: Debounced Sensor-Fusion Apogee Gate ----------
    // Three independent channels must all agree before the debounce timer runs:
    //   1. isDescending()   — N consecutive ticks of negative velocity (Flaw 2)
    //   2. hasPassedApogee()— velocity <= -2.0 m/s hysteresis threshold (Flaw 6)
    //   3. isNearFreefall() — IMU accel near 1g, confirming engine-off (Flaw 5)
    const bool descending     = isDescending(system);
    const bool past_apogee    = hasPassedApogee(system);
    const bool in_freefall    = isNearFreefall(system);

    if(descending && past_apogee && in_freefall)
    {
        // All channels agree: accumulate debounce time (Flaw 1).
        system.apogee_debounce_seconds +=
            system.delta_time_seconds;

        if(system.apogee_debounce_seconds >=
           FlightConfig::ASCENT_APOGEE_DEBOUNCE_SECONDS)
        {
            std::cout << "Apogee detected.\n";

            logEvent(system,
                     "APOGEE DETECTED: velocity + descent + freefall confirmed",
                     Event_Flight);

            system.current_state =
                Apogee_confirm;
        }
    }
    else
    {
        // Any channel failing resets the debounce — all conditions must be
        // sustained, not just momentarily present.
        if(system.apogee_debounce_seconds > 0.0f)
        {
            std::cout << "[ASCENT] Apogee candidate lost -- debounce reset\n";
        }

        system.apogee_debounce_seconds = 0.0f;
    }

    system.previous_altitude = altitude;
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
        system.apogee_climb_debounce_seconds = 0.0f;
    }

    // Flaw 1: Debounce climb recovery. Require continuous climb before rejecting.
    if(hasResumedClimb(system))
    {
        system.apogee_climb_debounce_seconds += system.delta_time_seconds;
        if(system.apogee_climb_debounce_seconds >= FlightConfig::APOGEE_CONFIRM_CLIMB_DEBOUNCE_SECONDS)
        {
            std::cout << "Apogee rejected; climb resumed.\n";
            logEvent(system, "APOGEE REJECTED: climb resumed", Event_Flight);
            system.current_state = Ascent;
            return;
        }
    }
    else
    {
        system.apogee_climb_debounce_seconds = 0.0f;
    }

    // Flaw 3: Backup deployment trigger if falling too fast
    if(system.vertical_velocity < FlightConfig::APOGEE_CONFIRM_EMERGENCY_DESCENT_VELOCITY_MPS)
    {
        std::cout << "EMERGENCY: Falling too fast without deployment!\n";
        logEvent(system, "EMERGENCY: Descent velocity exceeded threshold", Event_Fault);
        
        // Flaw 4: Deployment authorization safety
        if(system.system_armed && !system.payload_deployed)
        {
            system.current_state = Payload_Separation;
        }
        return;
    }

    // Flaw 2: Build confidence score via sustained checking (combines with ascent's debounce).
    // Wait for the duration, and verify we're still past apogee.
    if(stateElapsedMilliseconds(system) >=
       FlightConfig::APOGEE_CONFIRM_DURATION_MILLISECONDS &&
       hasPassedApogee(system))
    {
        logEvent(system,
                 "APOGEE CONFIRMED: deployment authorized",
                 Event_Flight);

        // Flaw 4: Deployment authorization safety
        if(system.system_armed && !system.payload_deployed)
        {
            system.current_state = Payload_Separation;
        }
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
        
        system.deployment_attempts = 0;
        system.last_deployment_attempt_time = 0.0f;
    }

    // Flaw 4: Deployment lockout
    if(system.payload_deployed)
    {
        system.current_state = Descent;
        return;
    }

    // Flaw 2: Retry mechanism
    if (system.deployment_attempts < FlightConfig::DEPLOYMENT_MAX_RETRIES)
    {
        if (system.deployment_attempts == 0 || 
            (stateElapsedSeconds(system) - system.last_deployment_attempt_time >= FlightConfig::DEPLOYMENT_RETRY_INTERVAL_SECONDS))
        {
            std::cout << "Attempting payload deployment (Try " << (system.deployment_attempts + 1) << ")...\n";
            triggerPayloadDeployment(system);
            system.last_deployment_attempt_time = stateElapsedSeconds(system);
            system.deployment_attempts++;
        }
    }

    // Flaw 1: Deployment verification
    if (isPayloadReleased(system))
    {
        std::cout << "Payload deployment verified.\n";
        system.current_state = Descent;
        return;
    }

    // Flaw 3: Timeout fault
    if(stateElapsedSeconds(system) >= FlightConfig::DEPLOYMENT_TIMEOUT_SECONDS)
    {
        raiseCriticalFault(system, "PAYLOAD_SEPARATION: Deployment confirmation timed out!");
        std::cout << "Deployment timeout! Proceeding to descent anyway.\n";
        // If we exhausted retries and it timed out, assume the worst and try to log descent anyway
        system.current_state = Descent;
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

    // Flaw 3: Descent anomaly detection
    if(!system.ballistic_descent_warning_issued && system.vertical_velocity < FlightConfig::DESCENT_BALLISTIC_WARNING_VELOCITY_MPS)
    {
        logEvent(system, "WARNING: Abnormal descent rate (ballistic)", Event_Fault);
        std::cout << "WARNING: Ballistic descent detected!\n";
        system.ballistic_descent_warning_issued = true;
    }

    // Flaw 1 & 2: Sustained stability + touchdown shock
    if(hasDetectedLanding(system) && hasLandingImpact(system))
    {
        logEvent(system,
                 "LANDING DETECTED: Shock and stability verified",
                 Event_Flight);
                 
        // Flaw 4: GPS recovery logging
        updateGPSReadings(system);
        std::cout << "Landing Coordinates: " << system.gps_latitude << ", " << system.gps_longitude << "\n";

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
        
        // Flaw 3: Safe-state verification
        std::cout << "Safing pyros and deployment mechanisms...\n";

        powerDownLandedSystems(system);
    }

    // Flaw 2: Post-flight data persistence
    saveFlightDataToSD(system);

    // Flaw 1: Wait for systems stabilized (faked here with time + power flags)
    if(stateElapsedSeconds(system) >=
       FlightConfig::LANDED_DURATION_SECONDS && system.flight_data_saved && system.landed_power_saving_applied)
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
        
        std::cout << "\n=== MISSION LOG ===\n";
        std::cout << "(Mission log has been saved to SD Card)\n";
        std::cout << "Mission Complete.\n";
        system.mission_complete = true;

        enableRecoveryBeacon(system);
        system.beacon_power_mode = 0; // High frequency
    }

    // Flaw 1: Beacon timeout is risky - run continuously
    // Flaw 2: Adaptive beacon power (fake logic based on time)
    if(stateElapsedSeconds(system) > 600.0f && system.beacon_power_mode == 0) // 10 minutes
    {
        std::cout << "[BEACON] Entering low power mode.\n";
        system.beacon_power_mode = 1;
    }

    // Flaw 3: GPS beacon telemetry
    // Periodically broadcast GPS (simulated by print every few seconds)
    if ((int)stateElapsedSeconds(system) % 5 == 0 && system.delta_time_seconds > 0)
    {
        updateGPSReadings(system);
        // We only want to print once per second tick, so we rely on delta time checks or similar, but
        // for simulation just printing is fine. Avoid spamming by doing it sparingly.
    }
}
