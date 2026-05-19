#pragma once

#include "system.hpp"

void initializeTiming(RocketSystem &system);
void updateTiming(RocketSystem &system);
void markStateEntry(RocketSystem &system);
long stateElapsedSeconds(const RocketSystem &system);
long stateElapsedMilliseconds(const RocketSystem &system);
