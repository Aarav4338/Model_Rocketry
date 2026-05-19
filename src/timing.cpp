#include "timing.hpp"

// Captures the starting wall-clock references for the desktop mission loop.
// Every subsystem then consumes time through `RocketSystem` instead of calling
// the clock independently.
void initializeTiming(RocketSystem &system)
{
    const auto now = std::chrono::steady_clock::now();

    system.mission_start_time = now;
    system.previous_update_time = now;
    system.current_time = now;
    system.state_entry_time = now;
    system.delta_time_seconds = 0.0f;
    system.mission_elapsed_seconds = 0.0f;
}

// Updates mission elapsed time and loop delta time once per cycle. The physics
// integrator uses delta time, while state handlers use elapsed mission or state
// time through this same subsystem.
void updateTiming(RocketSystem &system)
{
    system.previous_update_time = system.current_time;
    system.current_time = std::chrono::steady_clock::now();

    system.delta_time_seconds =
        std::chrono::duration<float>(
            system.current_time - system.previous_update_time
        ).count();

    system.mission_elapsed_seconds =
        std::chrono::duration<float>(
            system.current_time - system.mission_start_time
        ).count();
}

// Marks the moment a state became active. State handlers call this indirectly
// through their shared entry helper so duration checks use a common reference.
void markStateEntry(RocketSystem &system)
{
    system.state_entry_time = system.current_time;
}

// Returns whole seconds spent in the current state for ground and recovery
// phases whose timing does not need sub-second precision.
long stateElapsedSeconds(const RocketSystem &system)
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        system.current_time - system.state_entry_time
    ).count();
}

// Returns milliseconds spent in the current state for short confirmation
// windows, especially apogee confirmation.
long stateElapsedMilliseconds(const RocketSystem &system)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        system.current_time - system.state_entry_time
    ).count();
}
