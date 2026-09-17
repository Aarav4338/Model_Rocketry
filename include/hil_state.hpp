#pragma once

#ifdef ARDUINO

// Bridges the injected flight physics (src/simulation.cpp's updateSimulation(),
// which already writes true altitude/velocity/accel/battery straight into
// RocketSystem on every platform) into the HAL's sensor read functions in
// hal_esp8266.cpp, so that in HIL_MODE the barometer/IMU/GNSS/battery
// readings reported over telemetry are numerically consistent with the fake
// flight the FSM is reacting to — instead of the real, stationary sensor
// values that would otherwise leak into the CSV/radio telemetry while the
// FSM itself believes it's mid-flight.
//
// main.cpp populates this once per tick, right after calling
// updateSimulation(), and before calling updateSensors() (which triggers the
// Hardware::read*() calls this struct feeds).
struct HilBridge
{
    float true_altitude = 0.0f;
    float vertical_velocity = 0.0f;
    float accel_magnitude = 9.80665f;
    float battery_voltage = 7.4f;
};

extern HilBridge g_hil;

#endif
