#pragma once

#include "system.hpp"

void raiseFault(RocketSystem &system,
                const char *message,
                bool sensor_failure);

// Raises a critical fault that permanently blocks arming. Use this
// for conditions that make flight unsafe regardless of retry: dead IMU,
// depleted battery, timeout during prelaunch checks, etc.
void raiseCriticalFault(RocketSystem &system,
                        const char *message);

void checkWatchdog(RocketSystem &system);
