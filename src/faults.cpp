#include "faults.hpp"

#include <cstdio>

#include "config.hpp"
#include "logging.hpp"
#include "timing.hpp"

// Raises a fault without adding a fault state to the mission FSM. The fault path
// records the error, preserves whether a sensor caused it, and lets the main
// loop stop safely from any mission phase.
void raiseFault(RocketSystem &system,
                const char *message,
                bool sensor_failure)
{
    if(system.fault_detected)
    {
        return;
    }

    system.fault_detected = true;
    system.sensor_failure = system.sensor_failure ||
                            sensor_failure;

    std::snprintf(system.error_message,
                  FlightConfig::ERROR_MESSAGE_CAPACITY,
                  "%s",
                  message);

    logEvent(system,
             message,
             Event_Fault);
}

// Raises a fault that is unconditionally treated as mission-critical.
// Unlike raiseFault, this also sets has_critical_fault so that the
// prelaunch check (and any future state that cares about severity) can
// distinguish between a "we warned you" non-critical fault (e.g. telemetry
// degraded) and a "do not fly" hard abort (e.g. IMU dead, battery low).
void raiseCriticalFault(RocketSystem &system,
                        const char *message)
{
    system.has_critical_fault = true;
    raiseFault(system, message, true);
}

// Maps each mission state to its configured watchdog limit. Keeping these
// values in configuration preserves the ten-state FSM while still allowing
// independent safety supervision.
static long watchdogTimeoutForState(State state)
{
    switch(state)
    {
        case BOOT:
            return FlightConfig::WATCHDOG_BOOT_TIMEOUT_SECONDS;

        case TEST_MODE:
            return FlightConfig::WATCHDOG_TEST_MODE_TIMEOUT_SECONDS;

        case Prelaunch_Check:
            return FlightConfig::WATCHDOG_PRELAUNCH_TIMEOUT_SECONDS;

        case Launch_Pad:
            return FlightConfig::WATCHDOG_LAUNCH_PAD_TIMEOUT_SECONDS;

        case Ascent:
            return FlightConfig::WATCHDOG_ASCENT_TIMEOUT_SECONDS;

        case Apogee_confirm:
            return FlightConfig::WATCHDOG_APOGEE_CONFIRM_TIMEOUT_SECONDS;

        case Payload_Separation:
            return FlightConfig::WATCHDOG_PAYLOAD_SEPARATION_TIMEOUT_SECONDS;

        case Descent:
            return FlightConfig::WATCHDOG_DESCENT_TIMEOUT_SECONDS;

        case Landed:
            return FlightConfig::WATCHDOG_LANDED_TIMEOUT_SECONDS;

        case Beacon:
            return FlightConfig::WATCHDOG_BEACON_TIMEOUT_SECONDS;

        default:
            return 1;
    }
}

// Supervises the active state from outside the FSM. If a state stalls beyond
// its configured limit, the watchdog raises a fault instead of transitioning to
// an extra fault or abort state.
void checkWatchdog(RocketSystem &system)
{
    if(!system.watchdog_enabled ||
       system.fault_detected ||
       system.mission_complete)
    {
        return;
    }

    if(system.current_state != system.previous_state)
    {
        return;
    }

    const long timeout =
        watchdogTimeoutForState(system.current_state);

    if(stateElapsedSeconds(system) <= timeout)
    {
        return;
    }

    char message[FlightConfig::ERROR_MESSAGE_CAPACITY];

    std::snprintf(message,
                  sizeof(message),
                  "WATCHDOG: State %d timed out",
                  system.current_state);

    // Watchdog Fault Correction Mechanism
    // If we are in flight, we don't want the rocket to fall without control.
    // We forcefully advance the FSM to ensure safe recovery and landing.
    if(system.current_state == Ascent || system.current_state == Apogee_confirm)
    {
        system.current_state = Payload_Separation;
        logEvent(system, "WATCHDOG RECOVERY: Forced payload separation", Event_Fault);
        return; // Prevent terminal fault
    }
    else if(system.current_state == Descent)
    {
        system.current_state = Landed;
        logEvent(system, "WATCHDOG RECOVERY: Forced landed state", Event_Fault);
        return; // Prevent terminal fault
    }

    raiseFault(system,
               message,
               false);
}
