#pragma once

#include "system.hpp"

void raiseFault(RocketSystem &system,
                const char *message,
                bool sensor_failure);

void checkWatchdog(RocketSystem &system);
