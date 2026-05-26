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

// Returns true when the IMU accelerometer indicates near-freefall (engine off).
// Used as a third sensor-fusion channel in the apogee detection gate.
bool isNearFreefall(const RocketSystem &system);

// ---------- Prelaunch sensor checks ----------
// Populates imu_accel_magnitude, imu_gyro_rate, tilt_angle_deg, and
// battery_voltage from hardware (or simulation stubs). Call once per tick.
void updateIMUReadings(RocketSystem &system);

// Returns true when the IMU accelerometer magnitude is within the expected
// range for a grounded rocket (~1g) and not outputting nonsense values.
bool isIMUSane(const RocketSystem &system);

// Returns true when angular rate and linear acceleration indicate the
// rocket is standing still on the pad, not being handled or disturbed.
bool isStationary(const RocketSystem &system);

// Returns true when battery voltage is above the minimum safe threshold.
bool isBatteryOk(const RocketSystem &system);

// Returns true when the estimated tilt from vertical is within the
// allowed launch-pad alignment limit.
bool isVertical(const RocketSystem &system);
