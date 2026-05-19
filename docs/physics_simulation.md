# Physics Simulation

This document explains the simple vertical rocket physics used by the project.
The simulator is intentionally basic. It is meant to make the rocket move in a
believable way while keeping the avionics architecture clear.

The model includes:

- vertical acceleration
- vertical velocity
- altitude
- gravity
- powered ascent
- motor burnout
- coast
- natural apogee
- ballistic descent
- ground contact

The model does not include:

- drag
- wind
- atmospheric density
- rotation
- thrust vector control
- multi-axis motion
- CFD
- advanced aerodynamics

## Coordinate System

The simulation uses one vertical axis.

```text
upward motion   = positive
downward motion = negative
ground altitude = 0 meters
```

The rocket's altitude is stored in:

```text
system.simulated_true_altitude
```

The simulator's true vertical velocity is stored in:

```text
system.simulated_vertical_velocity
```

The simulator's current net vertical acceleration is stored in:

```text
system.vertical_acceleration
```

The mission FSM does not use `simulated_vertical_velocity` directly. The FSM
uses `system.vertical_velocity`, which is derived from filtered altitude by the
filter layer.

## The Integration Equations

Each loop has a measured time step:

```text
dt = system.delta_time_seconds
```

The simulator applies the basic kinematic integration:

```text
velocity += acceleration * dt
altitude += velocity * dt
```

In code, that means:

```text
system.simulated_vertical_velocity +=
    system.vertical_acceleration * system.delta_time_seconds;

system.simulated_true_altitude +=
    system.simulated_vertical_velocity * system.delta_time_seconds;
```

This is simple Euler integration. It is not a high-fidelity aerospace method,
but it is easy to understand and appropriate for an educational one-dimensional
simulator.

## Gravity

Gravity is a constant downward acceleration:

```text
FlightConfig::SIM_GRAVITY_MPS2 = 9.80665
```

Because upward is positive, gravity contributes:

```text
-9.80665 m/s^2
```

When the motor is not thrusting and the rocket is airborne, acceleration is:

```text
acceleration = -gravity
```

That naturally slows upward coast motion, makes velocity reach zero at apogee,
and then makes velocity negative during descent.

## Thrust

The project uses a simple thrust acceleration constant:

```text
FlightConfig::SIM_THRUST_ACCELERATION_MPS2
```

This value represents upward acceleration produced by the motor before gravity
is subtracted. During powered ascent:

```text
net_acceleration = thrust_acceleration - gravity
```

With the current defaults:

```text
net_acceleration = 30.0 - 9.80665
                 = 20.19335 m/s^2 upward
```

That means the rocket gains upward velocity while the motor is burning.

## Motor Burn

Motor burn is controlled by:

```text
system.thrust_active
system.motor_burn_time_remaining
system.burnout_detected
```

At ignition:

```text
thrust_active = true
motor_burn_time_remaining = SIM_MOTOR_BURN_DURATION_SECONDS
burnout_detected = false
```

Each loop subtracts `dt` from `motor_burn_time_remaining`. When the remaining
time reaches zero:

```text
thrust_active = false
motor_burn_time_remaining = 0
burnout_detected = true
```

The simulator logs:

```text
MOTOR BURNOUT DETECTED
```

After burnout, thrust is gone and gravity is the only acceleration.

## Ignition

The desktop simulator ignites the motor while the FSM is in `Launch_Pad`, the
system is armed, and the simulator's configured ignition delay has elapsed:

```text
FlightConfig::SIM_MOTOR_IGNITION_DELAY_MILLISECONDS
```

This delay is not a mission-state launch detector. It only decides when the
desktop physics model should start producing motion.

The FSM still transitions to `Ascent` only after filtered or derived sensor
evidence shows that launch has happened.

## Powered Ascent

Powered ascent begins when `igniteMotor()` sets:

```text
system.thrust_active = true
system.current_flight_phase = Flight_Powered_Ascent
```

During powered ascent:

```text
acceleration = thrust_acceleration - gravity
velocity increases upward
altitude increases upward
```

Conceptually:

```text
the motor pushes up harder than gravity pulls down
therefore upward velocity grows
therefore altitude rises faster and faster
```

The FSM sees this indirectly:

```text
simulation true altitude
-> raw_altitude
-> filtered_altitude
-> derived vertical_velocity
-> hasDetectedLaunch()
-> Ascent
```

## Coast

Coast begins after burnout while the rocket is still moving upward.

During coast:

```text
thrust_active = false
burnout_detected = true
simulated_vertical_velocity > 0
acceleration = -gravity
current_flight_phase = Flight_Coast
```

Conceptually:

```text
the motor is off
the rocket is still moving upward because it already has velocity
gravity reduces that upward velocity every frame
altitude keeps increasing, but more slowly each frame
```

No artificial altitude ceiling exists. Apogee happens only because gravity
reduces upward velocity to zero.

## Natural Apogee

Apogee is the top of the flight path. In this model, apogee happens when
vertical velocity crosses zero:

```text
vertical_velocity <= 0
```

There is no hardcoded apogee altitude.

The simulator naturally produces the true motion:

```text
simulated_vertical_velocity decreases under gravity
simulated_vertical_velocity eventually reaches 0
then it becomes negative
```

The FSM detects apogee using derived velocity from filtered altitude:

```text
system.vertical_velocity <= FlightConfig::VELOCITY_APOGEE_THRESHOLD_MPS
```

That check lives behind `hasPassedApogee()` in `src/sensors.cpp`.

## Apogee Confirmation

The FSM does not deploy immediately on the first zero or negative velocity
sample. It moves from `Ascent` to `Apogee_confirm` and waits a configured
confirmation window:

```text
FlightConfig::APOGEE_CONFIRM_DURATION_MILLISECONDS
```

During that window, it keeps checking:

```text
hasResumedClimb()
```

If derived velocity becomes positive again, the candidate apogee is rejected and
the FSM returns to `Ascent`.

This protects the deployment decision from a noisy velocity estimate or a brief
filter transient.

## Ballistic Descent

After apogee, velocity becomes negative:

```text
simulated_vertical_velocity <= 0
current_flight_phase = Flight_Ballistic_Descent
```

Acceleration remains:

```text
acceleration = -gravity
```

Conceptually:

```text
the rocket is falling
gravity continues to increase downward velocity
altitude decreases smoothly
```

There is no fake descent step and no instant direction change. The vehicle
falls because velocity has naturally gone negative.

## Ground Contact

The simulator prevents altitude from going below ground:

```text
if altitude <= 0 and velocity <= 0:
    altitude = 0
    velocity = 0
    acceleration = 0
```

This is a simple ground clamp. It is not landing detection for the FSM.

The FSM detects landing separately using sustained near-zero derived velocity
while in `Descent`.

## Landing Detection

Landing detection uses:

```text
system.landing_stationary_time_seconds
```

The filter layer increments that timer when:

```text
current_state == Descent
abs(derived_vertical_velocity) <= LANDING_STATIONARY_VELOCITY_MPS
```

If the velocity leaves the near-zero band, the timer resets.

The FSM calls landing detected when:

```text
landing_stationary_time_seconds >= LANDING_STATIONARY_DURATION_MILLISECONDS / 1000
```

This avoids exact altitude equality. Real barometric altitude can drift, so a
rocket should not require the filtered altitude to be exactly zero to declare
landing.

## Physics State Variables

`vertical_acceleration`

The current net vertical acceleration in meters per second squared. During
powered ascent it is thrust minus gravity. During coast and descent it is
negative gravity. On the ground it is zero.

`simulated_vertical_velocity`

The true velocity used internally by the simulator. It is integrated from
acceleration. The FSM does not use this directly.

`simulated_true_altitude`

The true altitude used internally by the simulator. It is integrated from true
velocity. The simulator converts it into `raw_altitude` for the sensor path.

`thrust_active`

True while the motor is burning.

`motor_burn_time_remaining`

Remaining burn duration. It counts down by `dt`.

`burnout_detected`

Latched true after motor burn reaches zero. It prevents the simple simulator
from re-igniting the motor.

`current_flight_phase`

A physical phase label used for telemetry. It does not control the mission FSM.

## Why Telemetry Uses Filtered Altitude

Telemetry reports altitude from `readAltitude(system)`, which returns
`filtered_altitude`. This matches the mission logic and makes telemetry show
the avionics view of the flight rather than the private simulator truth.

Telemetry also reports `vertical_acceleration`. That value comes from the
physics model, because the project does not yet include an accelerometer filter.
It is useful for learning how thrust, gravity, coast, and descent relate.

## Why This Is Believable But Still Simple

The flight now has the key shape of a vertical rocket flight:

```text
motor ignites
acceleration becomes strongly positive
velocity increases upward
altitude rises faster
motor burns out
acceleration becomes negative gravity
velocity decreases but altitude still rises
velocity crosses zero at apogee
velocity becomes negative
altitude falls
ground clamp stops the physics motion
landing logic confirms sustained near-zero motion
```

That is enough to teach avionics data flow and mission-state reasoning without
adding drag, wind, aerodynamic stability, or multi-axis dynamics.
