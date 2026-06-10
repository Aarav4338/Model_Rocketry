#pragma once

#include "system.hpp"

const char *eventCategoryName(EventCategory category);
void logEvent(RocketSystem &system,
              const char *message,
              EventCategory category = Event_System);

bool saveSystemState(const RocketSystem &system);
bool loadSystemState(RocketSystem &system);
