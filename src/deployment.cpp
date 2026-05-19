#include "deployment.hpp"

#include "logging.hpp"

// Abstracts the payload deployment action. The desktop build records the event,
// while an embedded target can replace the body with GPIO, servo, or pyro-safe
// hardware control without changing the mission state handler.
bool triggerPayloadDeployment(RocketSystem &system)
{
    system.payload_deployed = true;

    logEvent(system,
             "PAYLOAD DEPLOYED",
             Event_Deployment);

    return true;
}
