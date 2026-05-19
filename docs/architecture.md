# Physics-Based Rocket Flight Simulation Architecture

This project is an educational rocket flight computer simulator. Its full goal
is:

```text
Physics-Based Rocket Flight Simulation using FSM-Controlled Avionics Architecture
```

The project is not a full aerospace simulator. It intentionally models only
one-dimensional vertical motion using acceleration, velocity, gravity, motor
burn time, and time integration. The architecture around that simple physics is
the important part: simulation, filtering, mission states, telemetry, logging,
timing, sensors, deployment, power, faults, and configuration stay separated so
the code resembles a small embedded avionics project.

## Core Idea

The rocket moves because the simulation layer integrates physics:

```text
vertical_velocity += vertical_acceleration * dt
altitude += vertical_velocity * dt
```

The mission FSM does not own those physics equations. The FSM only asks
questions such as:

- Is the system armed?
- Has filtered sensor data shown launch motion?
- Has derived velocity crossed zero for apogee?
- Has upward motion resumed during apogee confirmation?
- Has derived velocity stayed near zero long enough to call the vehicle landed?

That separation keeps mission logic independent from how motion is generated.
Today the motion comes from `src/simulation.cpp`; later it could come from real
sensors behind the same subsystem interfaces.

## Execution Pipeline

Each loop in `src/main.cpp` runs the same ordered pipeline:

1. `updateTiming(system)`
2. Fault stop check
3. `updateSimulation(system)`
4. `filterAltitude(system)`
5. `updateVelocity(system)`
6. Optional `sendTelemetry(system)`
7. `dispatchState(system)`
8. `checkWatchdog(system)`
9. Desktop loop sleep

This order is deliberate. Physics creates raw altitude first. Filtering then
conditions it. Velocity is derived from filtered altitude. Telemetry reports the
latest processed state. Only then does the FSM make mission decisions. The
watchdog runs after the FSM so it supervises the current state without becoming
an extra mission state.

## Data Ownership

`RocketSystem` in `include/system.hpp` is the shared state object. It is passed
explicitly to every subsystem. That style avoids hidden global control flow and
keeps the project friendly to embedded C++.

Important groups inside `RocketSystem`:

- FSM state: `current_state`, `previous_state`
- Physical flight phase: `current_flight_phase`
- Mission flags: `system_armed`, `payload_deployed`, `mission_complete`
- Fault flags: `fault_detected`, `sensor_failure`, `watchdog_enabled`
- Sensor path: `raw_altitude`, `filtered_altitude`
- Derived flight data: `vertical_velocity`, `descending`
- Physics state: `vertical_acceleration`, `simulated_true_altitude`,
  `simulated_vertical_velocity`, `thrust_active`,
  `motor_burn_time_remaining`, `burnout_detected`
- Landing logic: `landing_stationary_time_seconds`
- Timing: `delta_time_seconds`, `mission_elapsed_seconds`,
  `state_entry_time`, `current_time`
- Telemetry/logging: `telemetry_sequence`, `mission_log`, `mission_history`

The split between true simulation values and derived avionics values matters.
`simulated_vertical_velocity` is the simulator's true velocity. The FSM does not
use it. The FSM uses `vertical_velocity`, which is estimated from filtered
altitude in `src/filters.cpp`.

## The Ten Mission FSM States

The FSM intentionally contains exactly ten mission states:

```text
BOOT
TEST_MODE
Prelaunch_Check
Launch_Pad
Ascent
Apogee_confirm
Payload_Separation
Descent
Landed
Beacon
```

Fault, abort, and safe states are not added to the FSM. Fault handling is an
independent supervision path in `src/faults.cpp`. This keeps the mission state
graph simple and preserves the required state list.

## Physical Flight Phases

The simulator also tracks a separate physical phase:

```text
Flight_Grounded
Flight_Powered_Ascent
Flight_Coast
Flight_Ballistic_Descent
Flight_Landed
```

These are not mission FSM states. They are telemetry labels describing the
physics model. For example, the FSM can still be in `Apogee_confirm` or
`Payload_Separation` while the physical flight phase is already
`Flight_Ballistic_Descent`.

## Simulation Layer

`src/simulation.cpp` owns all desktop physics:

- motor ignition after a configurable simulator delay
- motor burn remaining time
- thrust active flag
- burnout detection
- net vertical acceleration
- true vertical velocity
- true altitude
- ground clamp after impact
- deterministic optional sensor noise
- physical flight phase label

The simulator writes `raw_altitude`. It does not directly command FSM
transitions. That raw altitude then flows into filtering.

## Filtering Layer

`src/filters.cpp` performs two jobs:

1. Smooth raw altitude into `filtered_altitude`.
2. Estimate `vertical_velocity` from filtered altitude and `delta_time_seconds`.

It also derives:

- `descending`, used by apogee confirmation
- `landing_stationary_time_seconds`, used by landing detection

This means the FSM sees processed flight data, not simulator internals.

## Sensor Abstraction

`src/sensors.cpp` is the decision-facing sensor interface. Functions such as
`readAltitude()`, `hasDetectedLaunch()`, `hasPassedApogee()`, and
`hasDetectedLanding()` hide how flight evidence is produced.

In the desktop build, those functions read filtered values from `RocketSystem`.
On hardware, the same interface can sit above barometer, IMU, or fused sensor
data.

## FSM Layer

`src/states.cpp` owns mission behavior:

- initialize systems
- run diagnostics
- arm the system
- wait for launch evidence
- monitor ascent and coast
- confirm apogee
- deploy payload
- track descent
- power down after landing
- enable recovery beacon

The FSM does not compute gravity, motor burn, altitude integration, telemetry
formatting, storage formatting, or watchdog timeouts. It only moves through the
mission when filtered or derived evidence supports a transition.

## Telemetry Layer

`src/telemetry.cpp` builds `TelemetryPacket`. The packet includes:

- sequence number
- mission time
- FSM state
- physical flight phase
- filtered altitude
- derived vertical velocity
- vertical acceleration from the physics model
- arming status
- fault status
- descent status
- deployment status

The desktop version prints packets to the console. The packet structure keeps
the project ready for UART, USB, CAN, LoRa, or another embedded transport.

## Logging Layer

`src/logging.cpp` stores timestamped, categorized mission events. Important
events now include:

- system initialization
- diagnostics complete
- system armed
- motor ignition
- launch detected
- motor burnout detected
- apogee detected
- apogee confirmed
- payload deployed
- descent confirmed
- landing detected
- landed power-down
- recovery beacon enabled

The log is kept in fixed-size buffers to match the project's deterministic
embedded style.

## Timing Layer

`src/timing.cpp` centralizes:

- mission start time
- current time
- previous update time
- loop delta time
- mission elapsed time
- current state entry time
- state elapsed seconds
- state elapsed milliseconds

The physics integrator depends on `delta_time_seconds`. State durations depend
on state elapsed time. Having one timing subsystem prevents each state from
calling the clock independently.

## Fault Layer

`src/faults.cpp` keeps fault handling independent from mission states. The
watchdog checks whether the active state has exceeded its configured timeout and
raises a fault if needed. The main loop notices `fault_detected` and stops.

No fault state is added to the FSM.

## Deployment and Power Layers

`src/deployment.cpp` abstracts payload deployment. The desktop version sets
`payload_deployed` and logs the event. Future hardware code could drive a GPIO,
servo, MOSFET, or pyro-safe controller from this same function.

`src/power.cpp` abstracts landed power behavior. It disables high-rate flight
systems in the shared state and enables a recovery beacon later.

## Configuration

`include/config.hpp` centralizes tunable values:

- filter weights
- altitude zero clamp
- gravity
- thrust acceleration
- motor burn duration
- ignition delay
- loop sleep period
- sensor noise amplitude
- launch detection thresholds
- apogee velocity threshold
- landing stationary threshold and duration
- telemetry interval
- state durations
- watchdog limits
- fixed buffer sizes

The FSM and simulator therefore describe behavior, while tuning lives in one
obvious place.

## Why This Architecture Scales

Each subsystem has one reason to change:

- Physics changes belong in simulation.
- Mission rule changes belong in states or sensor guard helpers.
- Sensor noise and smoothing changes belong in filters.
- Hardware driver changes belong behind sensors, deployment, power, telemetry,
  or timing interfaces.
- Safety supervision changes belong in faults.
- Tuning changes belong in configuration.

That is the main lesson of the project. The physics is intentionally basic, but
the software boundaries make the small simulator feel like a real avionics
machine.
