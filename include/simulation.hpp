#pragma once

#include "system.hpp"

enum SimulationScenario
{
    SCENARIO_SUCCESS,
    SCENARIO_MOTOR_FAILURE,
    SCENARIO_SENSOR_FAILURE,
    SCENARIO_PARACHUTE_FAILURE
};

void updateSimulation(RocketSystem &system, SimulationScenario scenario);
