# Rocket Avionics

This project simulates the vertical flight of a model rocket using a finite state machine (FSM).

## How to Run

### 1. Build the Project
To compile the project manually using `g++`, run the following command from the root of the project directory:
```bash
g++ -std=c++17 -Wall -Wextra -pedantic -Iinclude \
  src/main.cpp src/states.cpp src/telemetry.cpp src/filters.cpp \
  src/logging.cpp src/simulation.cpp src/sensors.cpp src/timing.cpp \
  src/faults.cpp src/deployment.cpp src/power.cpp src/hal.cpp \
  src/telecommand.cpp -o rocket-avionics
```

### 2. Run the Simulation
After compilation, start the simulation by running:
```bash
./rocket-avionics
```
