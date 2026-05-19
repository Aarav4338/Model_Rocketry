# Development Log

This log records the major architecture and simulation changes made to turn the
project into a basic physics-based rocket flight simulation while preserving the
modular embedded-style avionics structure.

## Current Project Goal

The project now targets:

```text
Physics-Based Rocket Flight Simulation using FSM-Controlled Avionics Architecture
```

The simulator models vertical motion only. It uses acceleration, velocity,
gravity, thrust, motor burn time, and delta-time integration. It intentionally
does not model drag, wind, atmospheric density, rotation, CFD, thrust vector
control, or multi-axis dynamics.

## Architecture Preserved

- Preserved the exact ten mission FSM states:
  `BOOT`, `TEST_MODE`, `Prelaunch_Check`, `Launch_Pad`, `Ascent`,
  `Apogee_confirm`, `Payload_Separation`, `Descent`, `Landed`, and `Beacon`.
- Kept fault handling outside the FSM. The watchdog and fault latch remain in
  `src/faults.cpp`.
- Kept FSM decisions on filtered or derived data. The FSM uses filtered altitude
  and derived vertical velocity, not raw simulator values.
- Preserved subsystem boundaries for states, telemetry, logging, filtering,
  simulation, sensors, timing, faults, deployment, power, and configuration.
- Kept the implementation procedural and deterministic with explicit function
  calls and fixed-size buffers.

## Physics Simulation Added

- Replaced fake altitude stepping with kinematic integration:

```text
velocity += acceleration * dt
altitude += velocity * dt
```

- Added gravity through `FlightConfig::SIM_GRAVITY_MPS2`.
- Added thrust acceleration through
  `FlightConfig::SIM_THRUST_ACCELERATION_MPS2`.
- Added motor burn duration through
  `FlightConfig::SIM_MOTOR_BURN_DURATION_SECONDS`.
- Added simulator ignition delay through
  `FlightConfig::SIM_MOTOR_IGNITION_DELAY_MILLISECONDS`.
- Added true simulator velocity:
  `system.simulated_vertical_velocity`.
- Added net vertical acceleration:
  `system.vertical_acceleration`.
- Added thrust and burnout state:
  `system.thrust_active`, `system.motor_burn_time_remaining`, and
  `system.burnout_detected`.
- Added physical flight phase telemetry labels through `FlightPhase`.
- Added ground clamp so true altitude does not go below zero after ballistic
  descent.

## Launch Detection Preserved As Sensor-Based

The desktop simulator still decides when to ignite the motor, but that is only a
simulation event. The FSM does not enter `Ascent` because a timer expired.

`Launch_Pad` enters `Ascent` only when `hasDetectedLaunch()` sees:

```text
derived vertical velocity >= launch threshold
or
filtered altitude rise >= launch altitude delta threshold
```

This preserves the avionics rule that mission launch detection must be based on
processed flight evidence.

## Apogee Detection Improved

- Removed hardcoded apogee altitude behavior from the active simulator.
- Apogee now happens naturally when gravity reduces upward velocity to zero.
- `hasPassedApogee()` uses:

```text
system.vertical_velocity <= FlightConfig::VELOCITY_APOGEE_THRESHOLD_MPS
```

- `Ascent` logs:

```text
APOGEE DETECTED: vertical velocity crossed zero
```

- `Apogee_confirm` still rejects resumed upward motion using
  `hasResumedClimb()`.
- Deployment is authorized only after the configured millisecond confirmation
  window expires and velocity still indicates passed apogee.

## Ballistic Descent Added

After burnout and apogee:

```text
vertical_acceleration = -gravity
simulated_vertical_velocity < 0
simulated_true_altitude decreases smoothly
```

The vehicle no longer teleports from ascent to descent or follows a fake descent
rate. Descent is the natural result of gravity acting on velocity.

## Landing Detection Preserved As Velocity-Based

The simulation clamps true altitude at the ground, but the FSM does not use
exact altitude equality to detect landing.

Landing detection still uses:

```text
sustained near-zero derived vertical velocity
```

The filter layer accumulates `landing_stationary_time_seconds` only in the
`Descent` state and only while derived velocity is within the configured
near-zero band.

## Telemetry Updated

`TelemetryPacket` now includes:

- packet sequence
- mission time
- FSM state
- physical flight phase
- filtered altitude
- derived vertical velocity
- vertical acceleration
- armed flag
- fault flag
- descending flag
- payload deployed flag

Console telemetry now prints readable FSM state names and readable physical
flight phase names.

## Logging Updated

Meaningful mission events now include:

- `MOTOR IGNITION`
- `MOTOR BURNOUT DETECTED`
- `APOGEE DETECTED: vertical velocity crossed zero`
- `APOGEE CONFIRMED: deployment authorized`
- `DESCENT CONFIRMED`
- `LANDING DETECTED`

These are in addition to existing system, deployment, power, and beacon events.

## Filtering Cleanup

Added:

```text
FlightConfig::FILTER_ALTITUDE_ZERO_EPSILON_METERS
```

This clamps tiny floating-point residue near ground to zero when raw altitude is
also near zero. It keeps landed telemetry readable without affecting flight
detection thresholds.

## Desktop Loop Pacing

Added:

```text
FlightConfig::MAIN_LOOP_SLEEP_MILLISECONDS
```

The desktop loop sleeps briefly after each cycle. This keeps the simulation from
running as a tight busy loop and makes delta-time integration, filtered velocity,
telemetry, and logs easier to read. In a future embedded target, this pacing
would be replaced by a scheduler, timer interrupt, RTOS period, or HAL tick
loop.

## Root `main.cpp` Cleanup

The old root-level monolithic `main.cpp` contained obsolete architecture-first
altitude stepping. It has been replaced with a harmless marker translation unit.
The active program entry point is `src/main.cpp`.

This prevents the repository from containing two contradictory simulator
implementations.

## Documentation Added Or Rebuilt

Created or updated:

- `docs/architecture.md`
- `docs/physics_simulation.md`
- `docs/system_walkthrough.md`
- `docs/file_breakdown.md`
- `docs/development_log.md`

The documentation now explains:

- full project architecture
- purpose of every subsystem
- purpose of every source and header file
- important variables
- major functions
- data flow
- physics update flow
- FSM update flow
- timing flow
- telemetry flow
- logging flow
- sensor abstraction flow
- filtering flow
- mission-level execution trace from startup to completion

## Verification Performed

Compiled with warnings enabled:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic -Iinclude \
  src/main.cpp src/states.cpp src/telemetry.cpp src/filters.cpp \
  src/logging.cpp src/simulation.cpp src/sensors.cpp src/timing.cpp \
  src/faults.cpp src/deployment.cpp src/power.cpp -o rocket-avionics
```

The build completed without warnings.

Ran the simulator end to end. A representative mission log showed:

```text
BOOT: Systems initialized
TEST_MODE: Diagnostics complete
PRELAUNCH_CHECK: System armed
MOTOR IGNITION
LAUNCH DETECTED
MOTOR BURNOUT DETECTED
APOGEE DETECTED: vertical velocity crossed zero
APOGEE CONFIRMED: deployment authorized
PAYLOAD DEPLOYED
DESCENT CONFIRMED
LANDING DETECTED
LANDED: High-rate sensors and flight telemetry powered down
BEACON: Recovery beacon enabled
Mission Complete.
```

This verifies the intended mission sequence:

```text
startup
diagnostics
arming
simulated ignition
sensor-based launch detection
powered ascent
burnout
coast
natural apogee
apogee confirmation
deployment
ballistic descent
velocity-based landing detection
landed power-down
recovery beacon
mission completion
```

## Future Work

Possible future improvements that preserve the current boundaries:

- add a filtered acceleration path if accelerometer simulation is introduced
- require both acceleration and altitude-rate evidence for launch detection
- add configurable sample-count confidence for apogee confirmation
- add optional parachute descent modeling behind the simulation layer
- add persistent SD-card log export behind the logging interface
- add unit or trace tests for false launch, noisy apogee, and delayed ignition
- replace desktop timing with HAL tick or RTOS scheduling during STM32 migration
