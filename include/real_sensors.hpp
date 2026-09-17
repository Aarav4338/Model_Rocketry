#pragma once

#include "system.hpp"

// Bridges REAL hardware sensor readings (via the HAL) into the exact same
// RocketSystem fields that updateSimulation() fills on the desktop build:
// raw_altitude, imu_accel_magnitude, imu_gyro_rate, tilt_angle_deg,
// battery_voltage.
//
// This is what a real flight (HIL_MODE off, actually falling through the
// air) uses instead of updateSimulation(). It is deliberately kept in its
// own file rather than folded into sensors.cpp because it is a genuinely
// new code path: today nothing in this codebase ever converts a real
// barometer/IMU reading into the fields the FSM's launch/apogee/landing
// logic actually looks at (updateSensors() only ever populates the
// telemetry-facing pressure/temperature/accel_x-y-z fields, which the FSM
// does not read).
void updateRealSensorInputs(RocketSystem &system);
