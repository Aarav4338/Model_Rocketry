#include "deployment.hpp"

#include "hal.hpp"
#include "logging.hpp"

// Abstracts the payload deployment action. The desktop build records the event,
// while an embedded target can replace the body with GPIO, servo, or pyro-safe
// hardware control without changing the mission state handler.
//
// NOTE: this now actually calls into the HAL's actuation function. Previously
// this only set the payload_deployed flag and logged the event without ever
// firing Hardware::triggerDeploymentCharge() — harmless on desktop (the HAL
// stub just prints a line either way), but on the ESP8266 HIL build this is
// the only place that tells the real servo to move.
bool triggerPayloadDeployment(RocketSystem &system)
{
    Hardware::triggerDeploymentCharge();

    system.payload_deployed = true;

    logEvent(system,
             "PAYLOAD DEPLOYED",
             Event_Deployment);

    return true;
}
