# File Breakdown

This document explains every file in the project and how each one interacts
with the rest of the system.

## Root Files

### `AGENTS.md`

Purpose:

Defines project-specific coding instructions for automated coding agents.

Responsibilities:

- preserve the exact ten mission FSM states
- keep fault handling outside the FSM
- keep FSM decisions on filtered or derived flight data
- avoid timer-based launch detection in mission logic
- require active apogee rejection if climb resumes
- require sustained near-zero velocity for landing detection
- preserve subsystem boundaries
- place tunable values in `include/config.hpp`
- update documentation when architecture changes
- build with warnings before handoff

Interactions:

This file does not compile into the program. It controls how future changes
should be made.

### `README.md`

Purpose:

Introduces the project, build command, folder structure, and high-level design
intent.

Responsibilities:

- explain the project goal
- show how to compile and run the simulator
- list subsystems
- point readers to deeper documentation

Interactions:

Human-facing only. It should stay synchronized with the actual build files and
architecture.

### `main.cpp`

Purpose:

Retained as a harmless root-level translation unit for older IDE references.

Responsibilities:

- prevent stale monolithic mission logic from remaining in the repository
- avoid defining a second `main()`
- point maintainers toward `src/main.cpp`

Important function:

`rootMainTranslationUnitMarker()`

This function intentionally does nothing. It lets the file remain valid C++
without becoming part of the active simulator.

Interactions:

The active build task does not use this file. If a broad wildcard build includes
it, it will not conflict with `src/main.cpp`.

### `rocket-avionics`

Purpose:

Compiled executable produced by the build command.

Responsibilities:

- run the current modular desktop simulator

Interactions:

Generated artifact. It is not source code.

### `main`

Purpose:

Compiled executable artifact.

Responsibilities:

- run the current modular desktop simulator when built from the active `src/`
  sources

Interactions:

Generated artifact. It is not source code. The primary documented executable is
`rocket-avionics`, but this artifact is also built from the same source set.

### `.vscode/tasks.json`

Purpose:

Defines the Visual Studio Code build task.

Responsibilities:

- invoke `/usr/bin/g++`
- compile with `-std=c++17`
- enable warnings with `-Wall -Wextra -pedantic`
- include the `include/` directory
- compile the modular files in `src/`
- output `rocket-avionics`

Interactions:

This task is the IDE build path for the active modular simulator.

## Header Files

### `include/config.hpp`

Purpose:

Central configuration for tunable constants.

Responsibilities:

- filter weights
- altitude zero epsilon
- gravity
- thrust acceleration
- motor burn duration
- ground altitude
- simulator ignition delay
- desktop loop sleep period
- sensor noise amplitude
- launch detection thresholds
- apogee threshold
- landing velocity and duration thresholds
- telemetry interval
- state durations
- watchdog timeouts
- fixed buffer sizes

Important values:

`SIM_GRAVITY_MPS2`

Downward acceleration used during coast and descent.

`SIM_THRUST_ACCELERATION_MPS2`

Upward motor acceleration before gravity is subtracted.

`SIM_MOTOR_BURN_DURATION_SECONDS`

How long thrust remains active after ignition.

`SIM_MOTOR_IGNITION_DELAY_MILLISECONDS`

Desktop simulator delay before ignition while on the launch pad. This is not an
FSM launch detector.

`TELEMETRY_INTERVAL_SECONDS`

Rate limit for console telemetry.

Interactions:

Almost every subsystem includes this file. It prevents magic numbers from being
scattered through mission logic.

### `include/system.hpp`

Purpose:

Defines the shared mission data model.

Responsibilities:

- define the ten mission FSM states
- define physical flight phase labels
- define logging event categories
- define mission event records
- define `RocketSystem`

Important types:

`State`

The mission FSM state enum. It must remain exactly:

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

`FlightPhase`

Physical phase labels used by telemetry:

```text
Flight_Grounded
Flight_Powered_Ascent
Flight_Coast
Flight_Ballistic_Descent
Flight_Landed
```

These are not FSM states.

`RocketSystem`

The central state object passed to every subsystem.

Important variables:

`current_state`, `previous_state`

Track FSM state and state-entry detection.

`current_flight_phase`

Telemetry-facing physical phase from the simulation layer.

`raw_altitude`, `filtered_altitude`

Raw sensor-like altitude and processed avionics altitude.

`vertical_velocity`

Derived velocity from filtered altitude. Used by FSM guards.

`vertical_acceleration`

Net acceleration from the physics simulator. Used for telemetry and learning.

`simulated_true_altitude`, `simulated_vertical_velocity`

Private physics truth used by simulation.

`thrust_active`, `motor_burn_time_remaining`, `burnout_detected`

Motor state for powered ascent and burnout.

Interactions:

All source files include `system.hpp` directly or through subsystem headers.

### `include/simulation.hpp`

Purpose:

Declares the simulation update interface.

Important function:

`updateSimulation(RocketSystem &system)`

Advances the physics model and writes raw sensor-like altitude.

Interactions:

Called once per main loop from `src/main.cpp`.

### `include/timing.hpp`

Purpose:

Declares centralized timing functions.

Important functions:

`initializeTiming()`

Initializes mission time and state-entry references.

`updateTiming()`

Updates loop delta time and mission elapsed time.

`markStateEntry()`

Records when a state becomes active.

`stateElapsedSeconds()` and `stateElapsedMilliseconds()`

Provide state duration queries to FSM and watchdog logic.

Interactions:

Used by `main.cpp`, `states.cpp`, `faults.cpp`, and `simulation.cpp`.

### `include/sensors.hpp`

Purpose:

Declares sensor and flight-evidence helper interfaces.

Important functions:

`initialize_IMU()`, `initialize_telemetry()`, `initialize_SD_card()`

Desktop initialization stubs for future hardware-backed startup.

`readAltitude()`

Returns filtered altitude.

`hasDetectedLaunch()`

Checks derived velocity or filtered altitude rise.

`hasPassedApogee()`

Checks derived velocity crossing the apogee threshold.

`hasResumedClimb()`

Rejects false apogee if upward derived velocity returns.

`hasDetectedLanding()`

Checks the sustained near-zero velocity timer.

Interactions:

State handlers call these functions instead of reading raw fields directly.

### `include/filters.hpp`

Purpose:

Declares signal-conditioning functions.

Important functions:

`filterAltitude()`

Smooths raw altitude.

`updateVelocity()`

Derives vertical velocity, descent flag, and landing stationary time.

Interactions:

Called from the main loop before FSM dispatch.

### `include/states.hpp`

Purpose:

Declares all ten FSM state handlers.

Responsibilities:

- keep state functions visible to `dispatchState()`
- preserve explicit state-handler boundaries

Interactions:

Included by `src/main.cpp`; implemented by `src/states.cpp`.

### `include/telemetry.hpp`

Purpose:

Declares telemetry packet data and telemetry functions.

Important type:

`TelemetryPacket`

Contains sequence, mission time, FSM state, flight phase, altitude, velocity,
acceleration, arming status, fault status, descent status, and deployment
status.

Important functions:

`shouldSendTelemetry()`

Rate-limits telemetry.

`buildTelemetryPacket()`

Creates a transport-neutral packet.

`sendTelemetry()`

Prints the packet in the desktop build.

Interactions:

Called by `src/main.cpp`; reads values from `RocketSystem`.

### `include/logging.hpp`

Purpose:

Declares event logging interfaces.

Important functions:

`eventCategoryName()`

Converts categories to text.

`logEvent()`

Records timestamped mission events.

Interactions:

Used by states, simulation, deployment, power, and faults.

### `include/faults.hpp`

Purpose:

Declares independent fault handling.

Important functions:

`raiseFault()`

Records and latches a fault.

`checkWatchdog()`

Checks state timeout limits.

Interactions:

Called by the main loop after state dispatch. Fault handling is not an FSM
state.

### `include/deployment.hpp`

Purpose:

Declares payload deployment abstraction.

Important function:

`triggerPayloadDeployment()`

Sets deployment state and logs the deployment event.

Interactions:

Called by `runPayloadSeparationState()`.

### `include/power.hpp`

Purpose:

Declares landed power-management abstractions.

Important functions:

`powerDownLandedSystems()`

Disables high-rate landed systems in the shared state.

`enableRecoveryBeacon()`

Enables recovery beacon mode.

Interactions:

Called by `runLandedState()` and `runBeaconState()`.

## Source Files

### `src/main.cpp`

Purpose:

Active program entry point and main avionics loop.

Responsibilities:

- create initial `RocketSystem`
- run the ordered update pipeline
- stop on faults
- dispatch FSM states
- call the watchdog
- pace the desktop loop

Important functions:

`createInitialSystem()`

Initializes every mission, physics, telemetry, logging, fault, timing, and power
field.

`dispatchState()`

Switches over the ten FSM states and calls the matching handler.

`main()`

Runs the loop:

```text
timing -> simulation -> filtering -> velocity -> telemetry -> FSM -> watchdog
```

Interactions:

This file is the orchestrator. It does not own physics equations, state logic,
filter math, telemetry formatting, or watchdog policy.

### `src/simulation.cpp`

Purpose:

Physics-based desktop rocket motion.

Responsibilities:

- ignite the motor in the simulator
- apply thrust and gravity
- integrate acceleration into velocity
- integrate velocity into altitude
- count down motor burn
- detect burnout
- update physical flight phase
- clamp altitude at ground
- produce raw altitude for the sensor path
- add deterministic optional sensor noise

Important functions:

`shouldIgniteMotor()`

Returns true when the desktop simulator should start motor burn. It checks
mission state, arming, burn flags, and simulator ignition delay.

`igniteMotor()`

Sets thrust active, loads burn time, sets powered ascent phase, and logs motor
ignition.

`netVerticalAcceleration()`

Returns net acceleration for the current physics frame.

`integrateVerticalMotion()`

Applies:

```text
velocity += acceleration * dt
altitude += velocity * dt
```

`updateMotorBurn()`

Counts burn time down and logs burnout once.

`updateFlightPhase()`

Labels the physical motion for telemetry.

`simulatedNoiseSample()`

Produces deterministic optional altitude noise.

`updateSimulation()`

Public simulation entry point called by the main loop.

Interactions:

Writes `raw_altitude`; filters and FSM consume processed data later.

### `src/filters.cpp`

Purpose:

Signal conditioning and derived flight data.

Responsibilities:

- smooth raw altitude
- clamp tiny near-zero altitude residue
- estimate vertical velocity from filtered altitude
- derive descent status
- accumulate landing stationary time

Important functions:

`filterAltitude()`

Applies a weighted filter to raw altitude.

`updateVelocity()`

Computes derived vertical velocity and landing evidence.

Interactions:

Runs after simulation and before telemetry/FSM decisions.

### `src/sensors.cpp`

Purpose:

Sensor abstraction and mission guard helpers.

Responsibilities:

- provide startup stubs
- expose filtered altitude
- detect launch from processed evidence
- detect apogee from derived velocity
- reject resumed climb
- detect landing from stationary time

Important functions:

`readAltitude()`

Returns `filtered_altitude`.

`hasDetectedLaunch()`

Uses derived upward velocity or filtered altitude rise.

`hasPassedApogee()`

Uses `vertical_velocity <= threshold`.

`hasResumedClimb()`

Uses `vertical_velocity > threshold`.

`hasDetectedLanding()`

Uses sustained near-zero velocity time.

Interactions:

FSM state handlers call these helpers.

### `src/states.cpp`

Purpose:

Mission FSM behavior.

Responsibilities:

- implement all ten state handlers
- keep state entry behavior consistent
- move through mission phases
- request deployment and power actions
- log mission events

Important functions:

`enterState()`

Detects first loop in a state and marks entry time.

`runBootState()`

Initializes subsystems.

`runTestModeState()`

Runs time-gated diagnostics.

`runPrelaunchCheckState()`

Arms the system.

`runLaunchPadState()`

Captures pad reference altitude and waits for launch evidence.

`runAscentState()`

Monitors ascent/coast and detects apogee from derived velocity.

`runApogeeConfirmState()`

Rejects resumed climb or confirms apogee after the configured window.

`runPayloadSeparationState()`

Triggers deployment and waits before descent state.

`runDescentState()`

Logs descent confirmation and waits for landing detection.

`runLandedState()`

Requests landed power-down.

`runBeaconState()`

Enables recovery beacon and completes the mission.

Interactions:

Uses sensors, timing, deployment, power, and logging. It does not compute
physics or raw filtering.

### `src/telemetry.cpp`

Purpose:

Telemetry packet construction and console output.

Responsibilities:

- provide readable state names
- provide readable flight phase names
- rate-limit packets
- build packet snapshots
- print packet fields
- advance telemetry sequence

Important functions:

`stateName()`

Converts FSM enum to text.

`flightPhaseName()`

Converts physical flight phase enum to text.

`shouldSendTelemetry()`

Checks telemetry interval and enabled flags.

`buildTelemetryPacket()`

Copies current system values into `TelemetryPacket`.

`sendTelemetry()`

Prints the packet and updates telemetry timing.

Interactions:

Reads from the current system snapshot after filtering and before FSM dispatch.

### `src/logging.cpp`

Purpose:

Timestamped mission event logging.

Responsibilities:

- convert categories to text
- copy text into fixed buffers
- append printable log lines
- store structured mission history events

Important functions:

`eventCategoryName()`

Returns category strings such as `SYSTEM`, `FLIGHT`, or `FAULT`.

`copyText()`

Safely copies messages into fixed-size arrays.

`appendMissionLogLine()`

Appends formatted text into `mission_log`.

`logEvent()`

Records the event in structured history and printable log form.

Interactions:

Called by many subsystems. Uses mission elapsed time from the timing subsystem.

### `src/timing.cpp`

Purpose:

Central time source for the desktop simulator.

Responsibilities:

- initialize mission clock values
- compute delta time each loop
- compute mission elapsed time
- mark state entry time
- answer state elapsed time queries

Important functions:

`initializeTiming()`

Sets all time fields at startup.

`updateTiming()`

Computes `delta_time_seconds` and `mission_elapsed_seconds`.

`markStateEntry()`

Stores the current time as state entry.

`stateElapsedSeconds()`

Returns whole seconds in current state.

`stateElapsedMilliseconds()`

Returns milliseconds in current state.

Interactions:

Used by simulation, states, faults, and telemetry timing.

### `src/faults.cpp`

Purpose:

Independent fault and watchdog supervision.

Responsibilities:

- latch faults
- record sensor-failure status
- write fixed-size error messages
- log fault events
- map states to watchdog timeouts
- raise fault if a state stalls

Important functions:

`raiseFault()`

Sets `fault_detected`, stores the message, and logs the event.

`watchdogTimeoutForState()`

Returns configured timeout for each FSM state.

`checkWatchdog()`

Runs the independent timeout check.

Interactions:

Called from the main loop after state dispatch. It does not add fault states to
the FSM.

### `src/deployment.cpp`

Purpose:

Payload deployment abstraction.

Responsibilities:

- set `payload_deployed`
- log deployment

Important function:

`triggerPayloadDeployment()`

Currently records deployment. Later it can drive hardware.

Interactions:

Called by `runPayloadSeparationState()`.

### `src/power.cpp`

Purpose:

Post-landing power behavior abstraction.

Responsibilities:

- disable high-rate landed systems
- enable recovery beacon mode
- log power events

Important functions:

`powerDownLandedSystems()`

Clears active flight system flags and logs the landed power-down event.

`enableRecoveryBeacon()`

Sets `recovery_beacon_enabled` and logs beacon activation.

Interactions:

Called by `Landed` and `Beacon` state handlers.

## Documentation Files

### `docs/architecture.md`

Purpose:

High-level architecture explanation.

Responsibilities:

- explain subsystem boundaries
- explain data ownership
- explain the ten-state FSM
- explain physical flight phases
- explain why simulation, filtering, telemetry, logging, and faults are separate

Interactions:

Human-facing system overview.

### `docs/physics_simulation.md`

Purpose:

Physics guide for the vertical rocket model.

Responsibilities:

- explain acceleration, velocity, altitude, gravity, thrust, burn, coast,
  apogee, descent, and landing
- explain the integration equations mathematically and conceptually

Interactions:

Human-facing reference for `src/simulation.cpp`.

### `docs/system_walkthrough.md`

Purpose:

Detailed runtime walkthrough from startup to mission completion.

Responsibilities:

- explain exact loop order
- trace every mission phase
- identify files and functions active in each phase
- explain variables, logs, telemetry, physics, and transition conditions

Interactions:

Primary learning document for understanding the whole machine.

### `docs/file_breakdown.md`

Purpose:

This file.

Responsibilities:

- explain every project file
- document purpose, important functions, and interactions

Interactions:

Human-facing map of the repository.

### `docs/development_log.md`

Purpose:

Chronological engineering log of architectural and physics changes.

Responsibilities:

- record why changes were made
- preserve design reasoning
- note verification performed

Interactions:

Human-facing history for maintainers.

### `docs/mission_architecture.md`

Purpose:

Mission-oriented architecture guide retained from the earlier documentation set.

Responsibilities:

- explain mission philosophy
- describe state behavior
- connect software evidence to mission review concepts

Interactions:

Human-facing companion to the more detailed system walkthrough.

### `docs/added.txt`

Purpose:

Earlier change notes for architecture features.

Responsibilities:

- record historical improvements such as velocity estimation, launch detection,
  timing, configuration, logging, landing detection, watchdog, deployment, and
  power abstraction

Interactions:

Human-facing historical note. The newer architecture files are the primary
documentation source.
