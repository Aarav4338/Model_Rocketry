#include "timing.hpp"

#ifdef ARDUINO
#include <Arduino.h>
// On the ESP8266 there is no wall clock to snapshot at boot; "now" is just
// seconds since power-on, read from the same millis() counter every other
// timestamp in this file uses.
static MissionTimePoint nowSeconds()
{
    return static_cast<float>(millis()) / 1000.0f;
}
#endif

// Captures the starting wall-clock references for the desktop mission loop.
// Every subsystem then consumes time through `RocketSystem` instead of calling
// the clock independently.
void initializeTiming(RocketSystem &system)
{
#ifdef ARDUINO
    const auto now = nowSeconds();
#else
    const auto now = std::chrono::steady_clock::now();
#endif

    system.mission_start_time = now;
    system.previous_update_time = now;
    system.current_time = now;
    system.state_entry_time = now;
    system.delta_time_seconds = 0.0f;
    system.mission_elapsed_seconds = 0.0f;
    system.state_entry_time_seconds = 0.0f;
}

// Updates mission elapsed time and loop delta time once per cycle. The physics
// integrator uses delta time, while state handlers use elapsed mission or state
// time through this same subsystem.
void updateTiming(RocketSystem &system)
{
    system.previous_update_time = system.current_time;

#ifdef ARDUINO
    // On real hardware, delta time must track real elapsed seconds — every
    // debounce/timeout window in config.hpp (PRELAUNCH_DEBOUNCE_SECONDS,
    // LAUNCH_PAD_INHIBIT_SECONDS, etc.) is meaningless unless mission time
    // actually advances at wall-clock speed. This intentionally differs from
    // the desktop build below, which fast-forwards physics for fast console
    // demos and does not need to match real time.
    system.current_time = nowSeconds();
    system.delta_time_seconds = system.current_time - system.previous_update_time;

    // Guard against a zero/negative delta on the very first tick or after a
    // millis() rollover (~49.7 days of continuous uptime).
    if (system.delta_time_seconds <= 0.0f || system.delta_time_seconds > 1.0f)
    {
        system.delta_time_seconds = 0.02f;
    }
#else
    system.current_time = std::chrono::steady_clock::now();

    // Use a fixed 50Hz time step (0.02s) to decouple physics from wall-clock CPU execution
    // This allows the simulation to run instantly without causing derivative explosions.
    system.delta_time_seconds = 0.02f;
#endif

    system.mission_elapsed_seconds += system.delta_time_seconds;
}

// Marks the moment a state became active. State handlers call this indirectly
// through their shared entry helper so duration checks use a common reference.
void markStateEntry(RocketSystem &system)
{
    system.state_entry_time = system.current_time;
    system.state_entry_time_seconds = system.mission_elapsed_seconds;
}

// Returns whole seconds spent in the current state for ground and recovery
// phases whose timing does not need sub-second precision.
long stateElapsedSeconds(const RocketSystem &system)
{
    // Convert simulated state_elapsed_seconds which tracks delta additions
    // Wait, state entry is marked by current_time, so we must track state elapsed via delta additions!
    return static_cast<long>(system.mission_elapsed_seconds - system.state_entry_time_seconds);
}

// Returns milliseconds spent in the current state for short confirmation
// windows, especially apogee confirmation.
long stateElapsedMilliseconds(const RocketSystem &system)
{
    return static_cast<long>((system.mission_elapsed_seconds - system.state_entry_time_seconds) * 1000.0f);
}
