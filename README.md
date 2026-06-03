# Rocket Avionics

This project simulates the vertical flight of a model rocket using a finite state machine (FSM).

## How to Run

### 1. Build the Project
To compile the project manually using `g++`, run the following command from the root of the project directory:
```bash
g++ -std=c++17 -Wall -Wextra -pedantic -Iinclude \
  src/*.cpp -o rocket-avionics
```
*(Alternatively, you can use `make` if a Makefile is provided in the future).*

### 2. Run the Simulation
The simulation supports various failure scenarios to test the robustness of the avionics software. After compilation, run the simulation by specifying a scenario:

```bash
./rocket-avionics <scenario>
```

**Available Scenarios:**
- `./rocket-avionics success`    : Nominal Flight
- `./rocket-avionics motor`      : Motor Thrust Failure (Early Burnout)
- `./rocket-avionics sensor`     : Altimeter Sensor Failure (Flatline)
- `./rocket-avionics parachute`  : Parachute Deployment Failure (Ballistic)
- `./rocket-avionics all`        : Run all 4 scenarios sequentially
- `./rocket-avionics reset_test` : Processor Reset Test (Crash & Recovery)

### 3. Reviewing Output
Once the simulation completes, it generates two files on your desktop (simulating the SD card and telemetry systems):
- `data.csv`: IN-SPACe compliant CSV telemetry log.
- `sd_card_log.txt`: The onboard SD card log, which contains detailed FSM state transitions and mirrored telemetry data.
