# System Walkthrough

This is the ground-up walkthrough of the whole project. Read it as if you are
watching the flight computer execute one loop at a time.

The project is a small desktop simulation of rocket avionics. It combines:

- a ten-state mission finite state machine
- a one-dimensional physics simulator
- a sensor abstraction
- an altitude filter
- derived vertical velocity
- telemetry packets
- timestamped mission logging
- centralized timing
- independent watchdog fault handling
- deployment and landed power abstractions
- centralized configuration

The rocket motion is simple physics. The software architecture is the main
lesson.

## Mental Model

Think of the project as a machine with one shared memory block:

```text
RocketSystem system
```

Every subsystem receives a reference to that memory block. Each subsystem owns a
specific responsibility:

```text
timing      writes dt and elapsed time
simulation  writes true physics state and raw altitude
filters     writes filtered altitude, derived velocity, descent, landing timer
telemetry   reads system data and emits packets
states      reads processed flight evidence and changes mission state
faults      checks watchdog limits outside the FSM
logging     records timestamped events from all subsystems
```

The project avoids hidden control flow. There are no inheritance-heavy objects,
no dynamic allocation, and no background threads controlling mission state.

## Program Startup

Execution starts in:

```text
src/main.cpp
main()
```

The first important function is:

```text
createInitialSystem()
```

That function creates and initializes the entire `RocketSystem` structure.

At startup:

```text
current_state = BOOT
previous_state = Beacon
current_flight_phase = Flight_Grounded
system_armed = false
payload_deployed = false
mission_complete = false
fault_detected = false
thrust_active = false
burnout_detected = false
raw_altitude = 0
filtered_altitude = 0
vertical_velocity = 0
vertical_acceleration = 0
simulated_true_altitude = 0
simulated_vertical_velocity = 0
motor_burn_time_remaining = 0
landing_stationary_time_seconds = 0
telemetry_sequence = 0
```

`createInitialSystem()` then calls:

```text
initializeTiming(system)
```

That function lives in:

```text
src/timing.cpp
```

It captures the initial desktop clock time and sets:

```text
mission_start_time
previous_update_time
current_time
state_entry_time
delta_time_seconds = 0
mission_elapsed_seconds = 0
```

After this, `main()` enters the mission loop.

## The Main Loop Order

Every loop cycle in `main()` runs this order:

```text
updateTiming(system)
if fault detected, stop
updateSimulation(system)
filterAltitude(system)
updateVelocity(system)
if shouldSendTelemetry(system), sendTelemetry(system)
dispatchState(system)
checkWatchdog(system)
sleep for MAIN_LOOP_SLEEP_MILLISECONDS
```

This order is the heart of the architecture.

### Step 1: Timing

File:

```text
src/timing.cpp
```

Function:

```text
updateTiming(system)
```

What it does:

- moves `current_time` into `previous_update_time`
- reads the new desktop clock into `current_time`
- computes `delta_time_seconds`
- computes `mission_elapsed_seconds`

Why it exists:

The physics integrator needs a real `dt`. State durations and telemetry
intervals need elapsed time. Centralizing timing means no state handler has to
call the clock directly.

Outputs:

```text
system.delta_time_seconds
system.mission_elapsed_seconds
system.current_time
system.previous_update_time
```

### Step 2: Fault Stop Check

File:

```text
src/main.cpp
```

If `system.fault_detected` is true, the main loop prints the fault message and
breaks. Faults are not mission states. They are independent supervision.

### Step 3: Physics Simulation

File:

```text
src/simulation.cpp
```

Function:

```text
updateSimulation(system)
```

What it does:

- checks whether the motor should ignite
- integrates vertical acceleration into velocity
- integrates velocity into altitude
- counts down motor burn time
- detects burnout
- updates the physical flight phase
- writes sensor-like raw altitude
- optionally adds deterministic simulated sensor noise

Inputs:

```text
current_state
system_armed
state elapsed time
delta_time_seconds
thrust_active
motor_burn_time_remaining
simulated_vertical_velocity
simulated_true_altitude
```

Outputs:

```text
vertical_acceleration
simulated_vertical_velocity
simulated_true_altitude
thrust_active
motor_burn_time_remaining
burnout_detected
current_flight_phase
raw_altitude
simulation_step
```

Important point:

The simulator does not set the mission state to `Ascent`, `Apogee_confirm`, or
`Descent`. It only creates physical motion. The FSM must detect that motion
through filtered or derived data.

### Step 4: Altitude Filtering

File:

```text
src/filters.cpp
```

Function:

```text
filterAltitude(system)
```

What it does:

It smooths `raw_altitude` into `filtered_altitude`:

```text
filtered_altitude =
    filtered_altitude * FILTER_PREVIOUS_WEIGHT
  + raw_altitude * FILTER_RAW_WEIGHT
```

It also clamps tiny near-zero residue to exactly zero when raw altitude is also
near zero. That keeps landed telemetry readable.

Inputs:

```text
raw_altitude
filtered_altitude
FILTER_PREVIOUS_WEIGHT
FILTER_RAW_WEIGHT
FILTER_ALTITUDE_ZERO_EPSILON_METERS
```

Outputs:

```text
filtered_altitude
```

Why it exists:

Real sensor values are noisy. The FSM should not make launch, apogee, or
landing decisions from raw data.

### Step 5: Derived Velocity and Landing Evidence

File:

```text
src/filters.cpp
```

Function:

```text
updateVelocity(system)
```

What it does:

It estimates vertical velocity from filtered altitude:

```text
altitude_delta = filtered_altitude - last_filtered_altitude_for_velocity
vertical_velocity = altitude_delta / delta_time
```

It then updates:

```text
last_filtered_altitude_for_velocity
descending
landing_stationary_time_seconds
```

`descending` becomes true when filtered altitude says the vehicle is airborne
and derived velocity is less than or equal to the descent threshold.

`landing_stationary_time_seconds` increments only in the `Descent` FSM state and
only when derived vertical velocity is near zero.

Inputs:

```text
filtered_altitude
last_filtered_altitude_for_velocity
delta_time_seconds
launch_reference_altitude
current_state
LANDING_STATIONARY_VELOCITY_MPS
```

Outputs:

```text
vertical_velocity
last_filtered_altitude_for_velocity
descending
landing_stationary_time_seconds
```

Why it exists:

The FSM needs flight facts, not raw samples. Launch detection, apogee detection,
descent recognition, and landing detection all depend on filtered or derived
flight evidence.

### Step 6: Telemetry

File:

```text
src/telemetry.cpp
```

Functions:

```text
shouldSendTelemetry(system)
buildTelemetryPacket(system)
sendTelemetry(system)
```

`shouldSendTelemetry()` checks whether enough mission time has elapsed since
the last packet.

`buildTelemetryPacket()` snapshots:

```text
sequence
mission time
FSM state
physical flight phase
filtered altitude
derived vertical velocity
vertical acceleration
armed flag
fault flag
descending flag
payload deployed flag
```

`sendTelemetry()` prints the packet and updates:

```text
telemetry_sequence
last_telemetry_time_seconds
```

Why it exists:

Telemetry has its own packet structure so a future embedded transport can
serialize the same data without asking state handlers for fields directly.

### Step 7: FSM Dispatch

File:

```text
src/main.cpp
```

Function:

```text
dispatchState(system)
```

What it does:

It switches on `system.current_state` and calls exactly one state handler:

```text
runBootState()
runTestModeState()
runPrelaunchCheckState()
runLaunchPadState()
runAscentState()
runApogeeConfirmState()
runPayloadSeparationState()
runDescentState()
runLandedState()
runBeaconState()
```

The FSM state handlers live in:

```text
src/states.cpp
```

Important point:

The FSM is called after simulation, filtering, velocity derivation, and
telemetry. That means the FSM sees the newest processed flight evidence.

### Step 8: Watchdog

File:

```text
src/faults.cpp
```

Function:

```text
checkWatchdog(system)
```

What it does:

- skips checks if watchdog is disabled
- skips checks if a fault already exists
- skips checks if the mission is complete
- skips checks during the same loop where a state transition was made
- reads the timeout for the current state
- compares state elapsed seconds to that timeout
- calls `raiseFault()` if the state has stalled

Why it exists:

Fault handling remains outside the FSM. The project preserves the exact ten
mission states while still detecting stalled states.

## State Entry Handling

Every state handler begins by calling:

```text
enterState(system)
```

This helper lives inside `src/states.cpp`.

It checks:

```text
current_state != previous_state
```

If the state is newly entered, it:

```text
previous_state = current_state
markStateEntry(system)
returns true
```

State handlers use that `true` return to print entry messages, log one-time
events, capture reference altitude, trigger deployment, or power down systems.

## Mission Phase Trace

The following sections trace every mission phase.

## BOOT Phase

FSM state:

```text
BOOT
```

Files involved:

- `src/main.cpp`
- `src/states.cpp`
- `src/sensors.cpp`
- `src/logging.cpp`
- `src/timing.cpp`
- `src/faults.cpp`

Functions running:

- `main()`
- `updateTiming()`
- `updateSimulation()`
- `filterAltitude()`
- `updateVelocity()`
- `dispatchState()`
- `runBootState()`
- `initialize_IMU()`
- `initialize_telemetry()`
- `initialize_SD_card()`
- `logEvent()`
- `checkWatchdog()`

What starts the phase:

`createInitialSystem()` sets:

```text
current_state = BOOT
previous_state = Beacon
```

Because the previous state is different, the first call to `runBootState()` is a
state entry.

What happens:

1. `enterState()` marks BOOT entry time.
2. The console prints boot messages.
3. `initialize_IMU()` returns true.
4. `initialize_telemetry()` returns true.
5. `initialize_SD_card()` returns true.
6. `logEvent()` records system initialization.
7. The FSM sets `current_state = TEST_MODE`.

Variables updated:

```text
previous_state
state_entry_time
mission_log
mission_history
mission_event_count
current_state
```

Telemetry generated:

Telemetry may be emitted before or after BOOT depending on the configured
interval, but the first visible packets usually appear in `TEST_MODE` because
BOOT completes quickly.

Logs generated:

```text
BOOT: Systems initialized
```

Physics calculations:

The simulator is idle:

```text
thrust_active = false
simulated_true_altitude = 0
simulated_vertical_velocity = 0
vertical_acceleration = 0
raw_altitude = 0
```

FSM conditions checked:

`runBootState()` checks whether subsystem initialization succeeded.

Subsystem interactions:

BOOT uses sensor/storage/telemetry initialization abstractions and the logging
subsystem. It does not know hardware details.

What ends the phase:

All initialization functions return true, so BOOT sets:

```text
current_state = TEST_MODE
```

## TEST_MODE Phase

FSM state:

```text
TEST_MODE
```

Files involved:

- `src/main.cpp`
- `src/states.cpp`
- `src/timing.cpp`
- `src/telemetry.cpp`
- `src/logging.cpp`
- `src/faults.cpp`

Functions running:

- `updateTiming()`
- `updateSimulation()`
- `filterAltitude()`
- `updateVelocity()`
- `shouldSendTelemetry()`
- `sendTelemetry()`
- `runTestModeState()`
- `stateElapsedSeconds()`
- `logEvent()`
- `checkWatchdog()`

What starts the phase:

BOOT sets:

```text
current_state = TEST_MODE
```

On the next loop, `enterState()` marks `TEST_MODE` entry.

What happens:

The state represents ground diagnostics. It waits until:

```text
stateElapsedSeconds(system) >= TEST_MODE_DURATION_SECONDS
```

Variables updated:

```text
delta_time_seconds
mission_elapsed_seconds
telemetry_sequence
last_telemetry_time_seconds
current_state at phase end
```

Telemetry generated:

Packets show:

```text
State: TEST_MODE
Flight phase: Grounded
Altitude: 0
Velocity: 0
Acceleration: 0
Armed: 0
Fault: 0
```

Logs generated:

At phase end:

```text
TEST_MODE: Diagnostics complete
```

Physics calculations:

No thrust is active. The simulator remains grounded.

FSM conditions checked:

The state checks elapsed diagnostic time.

Subsystem interactions:

Timing controls the duration. Telemetry reports the idle ground state.

What ends the phase:

When the diagnostic duration expires, the FSM sets:

```text
current_state = Prelaunch_Check
```

## Prelaunch_Check Phase

FSM state:

```text
Prelaunch_Check
```

Files involved:

- `src/states.cpp`
- `src/timing.cpp`
- `src/telemetry.cpp`
- `src/logging.cpp`
- `src/faults.cpp`

Functions running:

- `runPrelaunchCheckState()`
- `stateElapsedSeconds()`
- `logEvent()`

What starts the phase:

`TEST_MODE` completes diagnostics and sets `current_state`.

What happens:

The state waits for the configured prelaunch check duration. When the duration
expires, it arms the system:

```text
system.system_armed = true
```

Arming means the software is allowed to look for launch evidence. It does not
mean launch has happened.

Variables updated:

```text
system_armed
current_state
mission_log
mission_history
```

Telemetry generated:

Before arming:

```text
Armed: 0
```

After arming and transition:

```text
Armed: 1
```

Logs generated:

```text
PRELAUNCH_CHECK: System armed
```

Physics calculations:

The rocket remains on the ground. The motor has not ignited.

FSM conditions checked:

The state checks elapsed prelaunch time.

Subsystem interactions:

Timing gates the ground check. Logging records the arming event.

What ends the phase:

When the duration expires:

```text
current_state = Launch_Pad
```

## Launch_Pad Phase

FSM state:

```text
Launch_Pad
```

Physical flight phase before ignition:

```text
Flight_Grounded
```

Physical flight phase after ignition:

```text
Flight_Powered_Ascent
```

Files involved:

- `src/main.cpp`
- `src/simulation.cpp`
- `src/filters.cpp`
- `src/sensors.cpp`
- `src/states.cpp`
- `src/telemetry.cpp`
- `src/logging.cpp`
- `src/timing.cpp`

Functions running:

- `updateSimulation()`
- `shouldIgniteMotor()`
- `igniteMotor()`
- `integrateVerticalMotion()`
- `updateMotorBurn()`
- `updateFlightPhase()`
- `filterAltitude()`
- `updateVelocity()`
- `runLaunchPadState()`
- `canEnterAscent()`
- `hasDetectedLaunch()`
- `readAltitude()`
- `logEvent()`

What starts the phase:

`Prelaunch_Check` arms the system and sets:

```text
current_state = Launch_Pad
```

On entry:

```text
launch_reference_altitude = readAltitude(system)
```

That reference is the filtered altitude considered "pad altitude" for this
mission.

What happens before ignition:

The simulator checks:

```text
current_state == Launch_Pad
system_armed == true
!thrust_active
!burnout_detected
stateElapsedMilliseconds >= SIM_MOTOR_IGNITION_DELAY_MILLISECONDS
```

Until that condition is true, the rocket remains grounded.

What happens at ignition:

`igniteMotor()` sets:

```text
thrust_active = true
motor_burn_time_remaining = SIM_MOTOR_BURN_DURATION_SECONDS
current_flight_phase = Flight_Powered_Ascent
```

It logs:

```text
MOTOR IGNITION
```

Then the simulator begins integrating motion:

```text
vertical_acceleration = SIM_THRUST_ACCELERATION_MPS2 - SIM_GRAVITY_MPS2
simulated_vertical_velocity += vertical_acceleration * dt
simulated_true_altitude += simulated_vertical_velocity * dt
raw_altitude = simulated_true_altitude + simulatedNoiseSample()
```

How launch is detected:

The FSM does not use the ignition timer. It calls:

```text
canEnterAscent(system)
```

That requires:

```text
system_armed == true
hasDetectedLaunch(system) == true
```

`hasDetectedLaunch()` checks filtered or derived evidence:

```text
vertical_velocity >= LAUNCH_DETECTION_VELOCITY_MPS
or
filtered_altitude - launch_reference_altitude
    >= LAUNCH_DETECTION_ALTITUDE_DELTA_METERS
```

Variables updated:

```text
launch_reference_altitude
thrust_active
motor_burn_time_remaining
vertical_acceleration
simulated_vertical_velocity
simulated_true_altitude
raw_altitude
filtered_altitude
vertical_velocity
current_flight_phase
current_state at phase end
```

Telemetry generated:

Before ignition:

```text
State: Launch_Pad
Flight phase: Grounded
Altitude: near 0
Velocity: near 0
Acceleration: 0
Armed: 1
```

After ignition but before FSM transition:

```text
State: Launch_Pad
Flight phase: Powered ascent
Altitude: rising
Velocity: positive
Acceleration: positive
Armed: 1
```

Logs generated:

```text
MOTOR IGNITION
LAUNCH DETECTED
```

Physics calculations:

Powered ascent begins. The vehicle accelerates upward because thrust
acceleration is larger than gravity.

FSM conditions checked:

Launch guard:

```text
canEnterAscent()
```

Subsystem interactions:

Simulation creates physical motion. Filters condition the resulting raw
altitude. Sensors expose launch evidence. States decide whether to enter
`Ascent`. Logging records ignition and launch.

What ends the phase:

Filtered or derived launch evidence becomes true, so `runLaunchPadState()` sets:

```text
current_state = Ascent
```

## Ascent Phase

FSM state:

```text
Ascent
```

Physical flight phase can be:

```text
Flight_Powered_Ascent
Flight_Coast
```

Files involved:

- `src/simulation.cpp`
- `src/filters.cpp`
- `src/sensors.cpp`
- `src/states.cpp`
- `src/telemetry.cpp`
- `src/logging.cpp`
- `src/timing.cpp`

Functions running:

- `updateSimulation()`
- `integrateVerticalMotion()`
- `updateMotorBurn()`
- `updateFlightPhase()`
- `filterAltitude()`
- `updateVelocity()`
- `runAscentState()`
- `canConfirmApogee()`
- `hasPassedApogee()`
- `hasResumedClimb()` later in the next state

What starts the phase:

`Launch_Pad` detects launch from processed flight evidence and sets:

```text
current_state = Ascent
```

What happens during powered ascent:

If the motor is still burning:

```text
thrust_active = true
vertical_acceleration = thrust - gravity
simulated_vertical_velocity increases
simulated_true_altitude rises faster each frame
motor_burn_time_remaining decreases by dt
```

What happens at burnout:

When burn time reaches zero:

```text
thrust_active = false
motor_burn_time_remaining = 0
burnout_detected = true
```

The simulator logs:

```text
MOTOR BURNOUT DETECTED
```

What happens during coast:

After burnout:

```text
vertical_acceleration = -gravity
simulated_vertical_velocity is still positive but decreasing
simulated_true_altitude still rises, but more slowly
current_flight_phase = Flight_Coast
```

How apogee is detected:

The FSM calls:

```text
canConfirmApogee(system)
```

That checks the filtered-data `descending` flag. `descending` comes from
`updateVelocity()` and is based on derived velocity, not raw simulator state.

If descent is indicated, the FSM also calls:

```text
hasPassedApogee(system)
```

That returns true when:

```text
vertical_velocity <= VELOCITY_APOGEE_THRESHOLD_MPS
```

Variables updated:

```text
motor_burn_time_remaining
thrust_active
burnout_detected
vertical_acceleration
simulated_vertical_velocity
simulated_true_altitude
raw_altitude
filtered_altitude
vertical_velocity
descending
previous_altitude
current_flight_phase
```

Telemetry generated:

During powered ascent:

```text
State: Ascent
Flight phase: Powered ascent
Altitude: increasing
Velocity: positive
Acceleration: positive
```

During coast:

```text
State: Ascent
Flight phase: Coast
Altitude: still increasing
Velocity: positive but decreasing
Acceleration: about -9.80665
```

Near apogee:

```text
State: Ascent
Flight phase: Ballistic descent
Velocity: approaches zero, then becomes negative
```

Logs generated:

```text
MOTOR BURNOUT DETECTED
APOGEE DETECTED: vertical velocity crossed zero
```

Physics calculations:

The key natural transition happens here:

```text
gravity slows upward velocity
velocity reaches zero
velocity becomes negative
```

No altitude comparison creates apogee.

FSM conditions checked:

```text
canConfirmApogee()
hasPassedApogee()
```

Subsystem interactions:

Simulation produces motion. Filtering and velocity derivation convert motion
into flight evidence. Sensors expose apogee helpers. The FSM records an apogee
candidate and moves to confirmation.

What ends the phase:

Derived vertical velocity crosses the apogee threshold and `runAscentState()`
sets:

```text
current_state = Apogee_confirm
```

## Apogee_confirm Phase

FSM state:

```text
Apogee_confirm
```

Physical flight phase:

```text
Flight_Ballistic_Descent
```

Files involved:

- `src/simulation.cpp`
- `src/filters.cpp`
- `src/sensors.cpp`
- `src/states.cpp`
- `src/timing.cpp`
- `src/logging.cpp`
- `src/telemetry.cpp`

Functions running:

- `updateSimulation()`
- `filterAltitude()`
- `updateVelocity()`
- `runApogeeConfirmState()`
- `hasResumedClimb()`
- `hasPassedApogee()`
- `stateElapsedMilliseconds()`
- `logEvent()`

What starts the phase:

`Ascent` detects an apogee candidate from derived velocity and sets:

```text
current_state = Apogee_confirm
```

What happens:

The rocket continues falling under gravity in the simulation. The FSM waits a
short confirmation window:

```text
APOGEE_CONFIRM_DURATION_MILLISECONDS
```

During that window it actively checks:

```text
hasResumedClimb(system)
```

If derived velocity becomes positive again:

```text
current_state = Ascent
```

That rejects the candidate.

If the window expires and velocity still indicates passed apogee:

```text
current_state = Payload_Separation
```

Variables updated:

```text
state_entry_time on entry
simulated_vertical_velocity
simulated_true_altitude
filtered_altitude
vertical_velocity
descending
current_state at phase end
```

Telemetry generated:

```text
State: Apogee_confirm
Flight phase: Ballistic descent
Altitude: near maximum, then decreasing
Velocity: negative or near zero
Acceleration: about -9.80665
```

Logs generated:

If rejected:

```text
APOGEE REJECTED: climb resumed
```

If confirmed:

```text
APOGEE CONFIRMED: deployment authorized
```

Physics calculations:

No special physics happens for apogee confirmation. The rocket is already in
ballistic descent. Confirmation is avionics logic, not a physics mode.

FSM conditions checked:

```text
hasResumedClimb()
stateElapsedMilliseconds() >= APOGEE_CONFIRM_DURATION_MILLISECONDS
hasPassedApogee()
```

Subsystem interactions:

Timing provides the confirmation window. Filters continue deriving velocity.
Sensors expose climb rejection and apogee checks. Logging records the decision.

What ends the phase:

The confirmation window expires and upward motion has not resumed, so the FSM
sets:

```text
current_state = Payload_Separation
```

## Payload_Separation Phase

FSM state:

```text
Payload_Separation
```

Physical flight phase:

```text
Flight_Ballistic_Descent
```

Files involved:

- `src/states.cpp`
- `src/deployment.cpp`
- `src/logging.cpp`
- `src/timing.cpp`
- `src/simulation.cpp`
- `src/filters.cpp`
- `src/telemetry.cpp`

Functions running:

- `runPayloadSeparationState()`
- `triggerPayloadDeployment()`
- `stateElapsedSeconds()`
- `canEnterDescent()`
- `updateSimulation()`
- `filterAltitude()`
- `updateVelocity()`

What starts the phase:

Apogee is confirmed and the FSM sets:

```text
current_state = Payload_Separation
```

What happens on entry:

`runPayloadSeparationState()` calls:

```text
triggerPayloadDeployment(system)
```

That function sets:

```text
payload_deployed = true
```

and logs:

```text
PAYLOAD DEPLOYED
```

What happens during the phase:

The FSM waits for:

```text
PAYLOAD_SEPARATION_DURATION_SECONDS
```

The simulation continues ballistic descent while deployment time elapses.

Variables updated:

```text
payload_deployed
mission_log
mission_history
simulated_vertical_velocity
simulated_true_altitude
raw_altitude
filtered_altitude
vertical_velocity
```

Telemetry generated:

```text
State: Payload_Separation
Flight phase: Ballistic descent
Payload deployed: true
Altitude: decreasing
Velocity: negative
Acceleration: about -9.80665
```

Logs generated:

```text
PAYLOAD DEPLOYED
```

Physics calculations:

The model does not include parachute drag. The rocket continues ballistic
descent under gravity.

FSM conditions checked:

```text
stateElapsedSeconds() >= PAYLOAD_SEPARATION_DURATION_SECONDS
canEnterDescent() == payload_deployed
```

Subsystem interactions:

The FSM requests deployment. Deployment logs and sets a flag. Timing controls
the phase duration. Simulation keeps falling.

What ends the phase:

Deployment duration expires and `payload_deployed` is true:

```text
current_state = Descent
```

## Descent Phase

FSM state:

```text
Descent
```

Physical flight phase:

```text
Flight_Ballistic_Descent
Flight_Landed after ground clamp
```

Files involved:

- `src/simulation.cpp`
- `src/filters.cpp`
- `src/sensors.cpp`
- `src/states.cpp`
- `src/logging.cpp`
- `src/telemetry.cpp`
- `src/timing.cpp`

Functions running:

- `updateSimulation()`
- `integrateVerticalMotion()`
- `updateFlightPhase()`
- `filterAltitude()`
- `updateVelocity()`
- `runDescentState()`
- `hasDetectedLanding()`
- `logEvent()`

What starts the phase:

Payload separation completes and the FSM sets:

```text
current_state = Descent
```

On entry, the FSM logs:

```text
DESCENT CONFIRMED
```

What happens while airborne:

The simulator continues:

```text
vertical_acceleration = -gravity
simulated_vertical_velocity becomes more negative
simulated_true_altitude decreases
raw_altitude follows simulated_true_altitude
```

What happens at the ground:

When true altitude reaches ground while velocity is downward:

```text
simulated_true_altitude = 0
simulated_vertical_velocity = 0
vertical_acceleration = 0
current_flight_phase = Flight_Landed
```

This is only the physics ground clamp.

How landing is detected:

`updateVelocity()` watches the derived velocity in the `Descent` state.

If:

```text
abs(vertical_velocity) <= LANDING_STATIONARY_VELOCITY_MPS
```

then:

```text
landing_stationary_time_seconds += delta_time_seconds
```

If velocity moves outside the band:

```text
landing_stationary_time_seconds = 0
```

`runDescentState()` calls:

```text
hasDetectedLanding(system)
```

That returns true when the stationary timer reaches the configured duration.

Variables updated:

```text
simulated_true_altitude
simulated_vertical_velocity
vertical_acceleration
current_flight_phase
raw_altitude
filtered_altitude
vertical_velocity
landing_stationary_time_seconds
current_state at phase end
```

Telemetry generated:

While airborne:

```text
State: Descent
Flight phase: Ballistic descent
Altitude: decreasing
Velocity: negative
Acceleration: about -9.80665
```

After ground clamp but before FSM landing confirmation:

```text
State: Descent
Flight phase: Landed
Altitude: approaching 0 through filter
Velocity: approaching 0 through filter
Acceleration: 0
```

Logs generated:

```text
DESCENT CONFIRMED
LANDING DETECTED
```

Physics calculations:

The vehicle falls ballistically until the ground clamp stops true motion. The
FSM waits for the filtered/derived motion estimate to settle near zero.

FSM conditions checked:

```text
hasDetectedLanding()
```

Subsystem interactions:

Simulation stops true motion at ground. Filtering smooths the sensor value.
Velocity derivation accumulates stationary time. Sensors expose landing status.
States transition only after sustained near-zero velocity.

What ends the phase:

Landing stationary time reaches the configured duration, so:

```text
current_state = Landed
```

## Landed Phase

FSM state:

```text
Landed
```

Physical flight phase:

```text
Flight_Landed
```

Files involved:

- `src/states.cpp`
- `src/power.cpp`
- `src/logging.cpp`
- `src/timing.cpp`
- `src/telemetry.cpp`

Functions running:

- `runLandedState()`
- `powerDownLandedSystems()`
- `stateElapsedSeconds()`
- `logEvent()`

What starts the phase:

`Descent` detects sustained near-zero derived velocity and sets:

```text
current_state = Landed
```

What happens on entry:

`runLandedState()` calls:

```text
powerDownLandedSystems(system)
```

That sets:

```text
imu_powered = false
high_rate_logging_enabled = false
flight_telemetry_enabled = false
landed_power_saving_applied = true
```

It logs:

```text
LANDED: High-rate sensors and flight telemetry powered down
```

Variables updated:

```text
imu_powered
high_rate_logging_enabled
flight_telemetry_enabled
landed_power_saving_applied
current_state at phase end
```

Telemetry generated:

Flight telemetry is disabled on entry. Recovery beacon telemetry can still run
after the beacon phase enables it.

Logs generated:

```text
LANDED: High-rate sensors and flight telemetry powered down
```

Physics calculations:

The rocket is physically stopped:

```text
altitude = 0
velocity = 0
acceleration = 0
```

FSM conditions checked:

The state waits for:

```text
LANDED_DURATION_SECONDS
```

Subsystem interactions:

The FSM requests landed power behavior. The power subsystem owns the flags and
log event.

What ends the phase:

After the landed duration:

```text
current_state = Beacon
```

## Beacon Phase

FSM state:

```text
Beacon
```

Physical flight phase:

```text
Flight_Landed
```

Files involved:

- `src/states.cpp`
- `src/power.cpp`
- `src/logging.cpp`
- `src/telemetry.cpp`
- `src/timing.cpp`

Functions running:

- `runBeaconState()`
- `enableRecoveryBeacon()`
- `stateElapsedSeconds()`
- `sendTelemetry()`

What starts the phase:

`Landed` waits its configured duration and sets:

```text
current_state = Beacon
```

What happens on entry:

The state calls:

```text
enableRecoveryBeacon(system)
```

That sets:

```text
recovery_beacon_enabled = true
```

and logs:

```text
BEACON: Recovery beacon enabled
```

Variables updated:

```text
recovery_beacon_enabled
mission_log
mission_history
mission_complete at phase end
```

Telemetry generated:

Because the recovery beacon is enabled, telemetry can continue even though
flight telemetry was powered down:

```text
State: Beacon
Flight phase: Landed
Altitude: 0
Velocity: 0
Acceleration: 0
```

Logs generated:

```text
BEACON: Recovery beacon enabled
```

Physics calculations:

No flight motion remains.

FSM conditions checked:

The state waits for:

```text
BEACON_DURATION_SECONDS
```

Subsystem interactions:

The power subsystem enables recovery signaling. The telemetry subsystem can
still report recovery packets. The logging subsystem provides the final mission
log.

What ends the phase:

After the beacon duration:

```text
mission_complete = true
```

The state prints:

```text
=== MISSION LOG ===
...
Mission Complete.
```

The main loop exits.

## Full Runtime Data Flow

Here is the full data path during flight:

```text
timing.cpp
  updateTiming()
  writes delta_time_seconds

simulation.cpp
  uses delta_time_seconds
  updates vertical_acceleration
  updates simulated_vertical_velocity
  updates simulated_true_altitude
  writes raw_altitude

filters.cpp
  reads raw_altitude
  writes filtered_altitude
  reads filtered_altitude and delta_time_seconds
  writes vertical_velocity
  writes descending
  writes landing_stationary_time_seconds

sensors.cpp
  exposes readAltitude()
  exposes hasDetectedLaunch()
  exposes hasPassedApogee()
  exposes hasResumedClimb()
  exposes hasDetectedLanding()

states.cpp
  reads sensor helpers
  changes current_state
  calls deployment and power abstractions
  records mission events

telemetry.cpp
  reads the current snapshot
  emits packet fields

faults.cpp
  reads current_state and state elapsed time
  raises fault if a state stalls
```

## Example Mission Event Order

A typical complete run produces mission events like:

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
```

This event order is useful because it proves the mission evolved through
physical evidence:

```text
ignition produced acceleration
acceleration produced velocity
velocity produced altitude rise
filtered altitude produced launch evidence
gravity produced velocity crossing zero
derived velocity produced apogee evidence
confirmation allowed deployment
ballistic descent reached ground
sustained near-zero derived velocity confirmed landing
```

## Why The FSM Does Not Read True Simulation Velocity

The simulator knows the perfect true velocity:

```text
system.simulated_vertical_velocity
```

The FSM intentionally does not read it. Real flight software would not know
perfect truth. It would know sensor readings and derived estimates.

So the FSM uses:

```text
system.vertical_velocity
```

That value is estimated by `updateVelocity()` from filtered altitude. This keeps
the architecture ready for real hardware, where altitude and acceleration
sensors replace the desktop simulator.

## Why Apogee Is Not A Hardcoded Altitude

The old architecture-oriented simulation could flip from ascent to descent at a
fixed altitude. The physics-based simulator does not do that.

Now apogee happens because:

```text
burnout removes thrust
gravity applies negative acceleration
upward velocity decreases
velocity reaches zero
velocity becomes negative
```

The FSM detects that with:

```text
hasPassedApogee()
```

which checks derived vertical velocity.

## Why Landing Is Not Exact Altitude Equality

The physics simulator clamps true altitude to ground, but the FSM does not
declare landing from:

```text
filtered_altitude == 0
```

Instead it uses:

```text
sustained near-zero vertical_velocity
```

This is more realistic because real barometric altitude can drift. A landed
rocket might not read exactly zero meters, but it should stop moving.

## How To Mentally Simulate One Frame

Pick any loop during flight and ask these questions in order:

1. How much time passed since the last loop?
2. Is thrust active?
3. What is acceleration this frame?
4. How does acceleration change true velocity?
5. How does velocity change true altitude?
6. What raw altitude does the sensor path see?
7. What filtered altitude does the avionics see?
8. What derived velocity comes from filtered altitude?
9. Does telemetry send a packet this frame?
10. Does the active FSM state see enough evidence to transition?
11. Did the watchdog detect a stalled state?

That sequence is the whole machine.
