#include "filters.hpp"

#include <cmath>

#include "config.hpp"
#include "sensors.hpp"

// Converts raw sensor-like altitude into the filtered altitude used by mission
// logic. The FSM never reads raw simulated altitude directly, which mirrors the
// real avionics rule that noisy sensor samples must be conditioned first.
void filterAltitude(RocketSystem &system)
{
    system.filtered_altitude =
        (system.filtered_altitude *
         FlightConfig::FILTER_PREVIOUS_WEIGHT) +
        (system.raw_altitude *
         FlightConfig::FILTER_RAW_WEIGHT);

    if(std::fabs(system.filtered_altitude) <=
           FlightConfig::FILTER_ALTITUDE_ZERO_EPSILON_METERS &&
       std::fabs(system.raw_altitude) <=
           FlightConfig::FILTER_ALTITUDE_ZERO_EPSILON_METERS)
    {
        system.filtered_altitude = 0.0f;
    }
}

// Derives vertical velocity and motion flags from filtered altitude. This keeps
// launch, apogee, descent, and landing guards based on processed flight data
// instead of simulator-only state or raw sensor values.
void updateVelocity(RocketSystem &system)
{
    const float current_altitude =
        readAltitude(system);

    const float altitude_delta =
        current_altitude -
        system.last_filtered_altitude_for_velocity;

    float delta_time =
        system.delta_time_seconds;

    if(delta_time <
       FlightConfig::MIN_VELOCITY_DELTA_TIME_SECONDS)
    {
        delta_time =
            FlightConfig::MIN_VELOCITY_DELTA_TIME_SECONDS;
    }

    system.vertical_velocity =
        altitude_delta / delta_time;

    system.last_filtered_altitude_for_velocity =
        current_altitude;

    const bool altitude_indicates_airborne =
        (current_altitude -
         system.launch_reference_altitude) >=
        FlightConfig::LAUNCH_DETECTION_ALTITUDE_DELTA_METERS;

    system.descending =
        altitude_indicates_airborne &&
        system.vertical_velocity <=
        FlightConfig::DESCENT_DETECTION_VELOCITY_MPS;

    if(system.current_state == Descent &&
       std::fabs(system.vertical_velocity) <=
       FlightConfig::LANDING_STATIONARY_VELOCITY_MPS)
    {
        system.landing_stationary_time_seconds +=
            system.delta_time_seconds;
    }
    else
    {
        system.landing_stationary_time_seconds = 0.0f;
    }
}
