#pragma once

#include "system.hpp"

// Processes an uplinked serial command string. It validates the command against
// current flight safety rules and modifies the RocketSystem state if accepted.
void processTelecommand(RocketSystem &system, const char *command);
