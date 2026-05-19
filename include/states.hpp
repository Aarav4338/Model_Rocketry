#pragma once

#include "system.hpp"

void runBootState(RocketSystem &system);
void runTestModeState(RocketSystem &system);
void runPrelaunchCheckState(RocketSystem &system);
void runLaunchPadState(RocketSystem &system);
void runAscentState(RocketSystem &system);
void runApogeeConfirmState(RocketSystem &system);
void runPayloadSeparationState(RocketSystem &system);
void runDescentState(RocketSystem &system);
void runLandedState(RocketSystem &system);
void runBeaconState(RocketSystem &system);
