# Mission Architecture Walkthrough

This document explains the mission-level architecture of the rocket avionics
simulator. For the deepest execution trace, read `docs/system_walkthrough.md`.
For the physics model, read `docs/physics_simulation.md`.

The project is educational software. It does not replace real launch
permission, range safety review, motor safety procedures, recovery planning, or
local authority approvals.

## Mission Philosophy

The mission software follows one rule:

```text
raw or perfect simulator data should not directly command mission transitions
```

The data path is:

```text
physics simulation or future hardware
-> raw sensor-like data
-> filtering
-> derived flight parameters
-> mission FSM guards
-> telemetry and event logs
```

This matters because real flight sensors are imperfect. Barometers drift, IMUs
vibrate, and sample timing varies. The FSM should react to processed flight
evidence such as filtered altitude, derived vertical velocity, descending
status, apogee candidate, resumed climb, and landing stationary time.

## The Ten Mission States

The mission FSM contains exactly these ten states:

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

Fault handling is not a mission state. Watchdog and fault logic live outside the
FSM so faults can be detected from any phase without expanding the state graph.

## Physical Flight Phases Are Separate

The simulator also labels the physical motion:

```text
Flight_Grounded
Flight_Powered_Ascent
Flight_Coast
Flight_Ballistic_Descent
Flight_Landed
```

These labels are used for telemetry. They are not additional FSM states.

For example, the FSM may be in `Payload_Separation` while the physical flight
phase is already `Flight_Ballistic_Descent`.

## State-by-State Behavior

`BOOT`

Initializes the IMU, telemetry, and SD-card abstractions. A failed IMU
initialization raises a fault without adding a fault state.

`TEST_MODE`

Represents ground diagnostics. It is time-gated because diagnostics are a
ground operation, not a flight-event detection problem.

`Prelaunch_Check`

Arms the system after the configured prelaunch duration. Arming allows the
software to look for launch evidence, but it does not mean launch has happened.

`Launch_Pad`

Captures the launch reference altitude and waits for launch evidence. The
desktop simulator may ignite the motor after a configurable delay, but the FSM
enters `Ascent` only when filtered altitude or derived velocity shows actual
motion.

`Ascent`

Monitors powered ascent and coast. The motor burns for a configured duration,
then burnout removes thrust. Gravity slows the rocket naturally. The FSM detects
an apogee candidate when derived vertical velocity crosses zero.

`Apogee_confirm`

Waits a millisecond-scale confirmation window and actively rejects the candidate
if upward motion resumes. Deployment is allowed only after apogee remains
confirmed.

`Payload_Separation`

Calls the deployment abstraction. Today this sets `payload_deployed` and logs
the event. Later it can drive real hardware behind the same interface.

`Descent`

Tracks the falling rocket after deployment. The simulator is ballistic, so
descent is driven by gravity. The FSM detects landing from sustained near-zero
derived velocity, not exact altitude equality.

`Landed`

Requests landed power reduction through the power subsystem. High-rate flight
systems are disabled in the shared state.

`Beacon`

Enables recovery beacon mode and prints the mission log before marking the
mission complete.

## Physics And Mission Logic

The physics model lives in `src/simulation.cpp`.

It updates:

```text
vertical_acceleration
simulated_vertical_velocity
simulated_true_altitude
thrust_active
motor_burn_time_remaining
burnout_detected
current_flight_phase
raw_altitude
```

The mission FSM does not read true simulator velocity. It reads the derived
velocity produced by `src/filters.cpp`:

```text
raw_altitude
-> filtered_altitude
-> vertical_velocity
-> FSM guards
```

This separation keeps the FSM portable. A future hardware version can replace
the simulator with real sensors while preserving the same state logic.

## Launch Detection

A launch timer is not used by the FSM. The simulator's ignition delay only
starts the desktop physics event.

`Launch_Pad` enters `Ascent` when:

```text
system_armed == true
and
(
    vertical_velocity >= LAUNCH_DETECTION_VELOCITY_MPS
    or
    filtered_altitude - launch_reference_altitude
        >= LAUNCH_DETECTION_ALTITUDE_DELTA_METERS
)
```

This makes launch detection sensor-based from the FSM's point of view.

## Apogee Detection

Apogee is not a hardcoded altitude. It happens naturally:

```text
motor burnout removes thrust
gravity slows upward velocity
upward velocity reaches zero
velocity becomes negative
```

The FSM sees that through:

```text
vertical_velocity <= VELOCITY_APOGEE_THRESHOLD_MPS
```

Then `Apogee_confirm` rejects resumed upward motion before deployment.

## Landing Detection

The simulator clamps true altitude at the ground, but the FSM does not require:

```text
filtered_altitude == 0
```

Landing detection uses:

```text
abs(vertical_velocity) <= LANDING_STATIONARY_VELOCITY_MPS
for LANDING_STATIONARY_DURATION_MILLISECONDS
```

This is closer to real barometric behavior because the measured ground altitude
may not be exactly zero after flight.

## Mission Evidence

The telemetry and log layers make the mission traceable.

Telemetry reports:

- FSM state
- physical flight phase
- filtered altitude
- derived velocity
- acceleration
- arming state
- fault state
- descent state
- deployment state

Mission logs record events such as:

- system initialized
- system armed
- motor ignition
- launch detected
- burnout detected
- apogee detected
- apogee confirmed
- payload deployed
- descent confirmed
- landing detected
- landed power-down
- recovery beacon enabled

These logs make it possible to reconstruct the mission sequence after a run.

## Guideline Context

Model rocketry and sub-orbital launch activity can require permissions,
notifications, local approvals, safety reviews, and recovery planning depending
on jurisdiction and vehicle class. The software helps create technical evidence:

- state history
- telemetry values
- deployment event timing
- fault status
- recovery beacon status
- mission logbook entries

It does not calculate safe range, wind drift, structural margin, launch-site
clearance, legal approval status, or recovery-zone safety. Those remain
real-world mission planning tasks.

## Future Improvements

Possible next steps that preserve the architecture:

- add filtered acceleration as a derived flight parameter
- require acceleration plus altitude-rate agreement for launch detection
- add sample-count confidence to apogee confirmation
- add optional parachute descent physics inside the simulation layer
- add automated trace tests for delayed ignition and noisy apogee
- replace desktop timing with HAL ticks or an RTOS scheduler
