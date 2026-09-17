#include "real_sensors.hpp"

#include <cmath>

#include "config.hpp"
#include "hal.hpp"

// Standard barometric formula (ISA, troposphere), converting absolute
// pressure to altitude above the point where pressure_pa == sea_level_pa.
// Using the *pad* pressure as the zero-reference (rather than the fixed
// 101325 Pa "sea level" constant) means this works indoors/at-altitude too
// -- the launch pad itself becomes altitude 0, same as the rest of the FSM
// already assumes via launch_reference_altitude.
static float pressureToRelativeAltitude(float pressure_pa, float pad_pressure_pa)
{
    if (pad_pressure_pa <= 0.0f)
    {
        return 0.0f;
    }

    return 44330.0f *
           (1.0f - std::pow(pressure_pa / pad_pressure_pa, 0.1902949f));
}

// One-time captured pad pressure, used as the zero-altitude reference for
// pressureToRelativeAltitude(). Populated lazily on first call.
static float g_pad_pressure_pa = 0.0f;
static bool g_pad_pressure_captured = false;

void updateRealSensorInputs(RocketSystem &system)
{
    float pressure_pa = 0.0f;
    float temperature_c = 0.0f;

    if (Hardware::readBarometer(pressure_pa, temperature_c))
    {
        if (!g_pad_pressure_captured)
        {
            g_pad_pressure_pa = pressure_pa;
            g_pad_pressure_captured = true;
        }

        system.raw_altitude =
            pressureToRelativeAltitude(pressure_pa, g_pad_pressure_pa);
    }

    float ax = 0.0f, ay = 0.0f, az = 0.0f, gx = 0.0f, gy = 0.0f, gz = 0.0f;
    if (Hardware::readIMU(ax, ay, az, gx, gy, gz))
    {
        system.accel_x = ax;
        system.accel_y = ay;
        system.accel_z = az;

        system.imu_accel_magnitude =
            std::sqrt(ax * ax + ay * ay + az * az);

        system.imu_gyro_rate =
            std::sqrt(gx * gx + gy * gy + gz * gz);

        system.gyro_spin_rate = gz;

        // Tilt from vertical, assuming the IMU's Z axis is mounted along the
        // rocket's long axis (nose-up on the pad). If your MPU6050 is
        // mounted differently, swap which axis this uses.
        const float accel_mag = system.imu_accel_magnitude;
        if (accel_mag > 0.001f)
        {
            float cos_tilt = az / accel_mag;
            if (cos_tilt > 1.0f) cos_tilt = 1.0f;
            if (cos_tilt < -1.0f) cos_tilt = -1.0f;
            system.tilt_angle_deg =
                std::acos(cos_tilt) * (180.0f / 3.14159265f);
        }

        system.roll = system.tilt_angle_deg;
        system.pitch = 0.0f;
        system.yaw = 0.0f;
    }

    system.battery_voltage = Hardware::readBatteryVoltage();
}
