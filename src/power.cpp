#include "power.hpp"

#include "logging.hpp"

// Applies landed power savings through flags in the desktop simulator. On
// hardware this is where high-rate sensors, logging, or flight radios would be
// disabled after landing.
void powerDownLandedSystems(RocketSystem &system)
{
    if(system.landed_power_saving_applied)
    {
        return;
    }

    system.imu_powered = false;
    system.high_rate_logging_enabled = false;
    system.flight_telemetry_enabled = false;
    system.landed_power_saving_applied = true;

    logEvent(system,
             "LANDED: High-rate sensors and flight telemetry powered down",
             Event_System);
}

// Enables recovery signaling after the flight systems have been reduced. Keeping
// this separate from flight telemetry mirrors a real post-landing recovery mode.
void enableRecoveryBeacon(RocketSystem &system)
{
    if(system.recovery_beacon_enabled)
    {
        return;
    }

    system.recovery_beacon_enabled = true;

    logEvent(system,
             "BEACON: Recovery beacon enabled",
             Event_System);
}
