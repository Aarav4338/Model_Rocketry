#pragma once

#include "system.hpp"

bool initialize_IMU();
bool initialize_telemetry();
bool initialize_SD_card();

float readAltitude(RocketSystem &system);
bool isDescending(RocketSystem &system);

bool canEnterAscent(RocketSystem &system);
bool hasDetectedLaunch(RocketSystem &system);
bool canConfirmApogee(RocketSystem &system);
bool hasPassedApogee(RocketSystem &system);
bool hasResumedClimb(RocketSystem &system);
bool canEnterDescent(RocketSystem &system);
bool hasDetectedLanding(RocketSystem &system);
