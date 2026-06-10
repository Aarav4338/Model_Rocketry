#include "logging.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

// Converts the compact event category enum into readable log text. Keeping this
// in the logging subsystem means other files record event intent without owning
// formatting rules.
const char *eventCategoryName(EventCategory category)
{
    switch(category)
    {
        case Event_System:
            return "SYSTEM";

        case Event_State:
            return "STATE";

        case Event_Flight:
            return "FLIGHT";

        case Event_Deployment:
            return "DEPLOYMENT";

        case Event_Fault:
            return "FAULT";

        default:
            return "UNKNOWN";
    }
}

// Copies bounded text into fixed-size buffers. The project avoids dynamic
// allocation so the same pattern remains suitable for embedded storage.
static void copyText(char *destination,
                     int capacity,
                     const char *source)
{
    if(capacity <= 0)
    {
        return;
    }

    std::snprintf(destination,
                  static_cast<std::size_t>(capacity),
                  "%s",
                  source);
}

// Appends one formatted line to the in-memory mission log. This desktop buffer
// stands in for the SD-card or flash log that an embedded target could write.
static void appendMissionLogLine(RocketSystem &system,
                                 const char *line)
{
    if(system.mission_log_length >=
       FlightConfig::MISSION_LOG_CAPACITY - 1)
    {
        return;
    }

    const std::size_t remaining =
        FlightConfig::MISSION_LOG_CAPACITY -
        system.mission_log_length;

    const int written =
        std::snprintf(system.mission_log + system.mission_log_length,
                      remaining,
                      "%s\n",
                      line);

    if(written <= 0)
    {
        return;
    }

    const std::size_t used =
        static_cast<std::size_t>(written);

    if(used >= remaining)
    {
        system.mission_log_length =
            FlightConfig::MISSION_LOG_CAPACITY - 1;
    }
    else
    {
        system.mission_log_length += used;
    }
}

// Records a timestamped mission event in both structured history and printable
// log form. All subsystems use this one entry point so mission reconstruction
// has a consistent time base and category vocabulary.
void logEvent(RocketSystem &system,
              const char *message,
              EventCategory category)
{
    if(system.mission_event_count <
       FlightConfig::MISSION_HISTORY_CAPACITY)
    {
        MissionEvent &event =
            system.mission_history[system.mission_event_count];

        event.timestamp_seconds =
            system.mission_elapsed_seconds;

        event.category = category;

        copyText(event.message,
                 FlightConfig::EVENT_MESSAGE_CAPACITY,
                 message);

        system.mission_event_count++;
    }

    char line[160];

    std::snprintf(line,
                  sizeof(line),
                  "[%07.2fs] [%s] %s",
                  system.mission_elapsed_seconds,
                  eventCategoryName(category),
                  message);

    appendMissionLogLine(system,
                         line);
}

// Simulates dumping critical volatile RAM into an EEPROM or Flash sector.
// Used for telemetry backup and data recovery strategy after a crash.
bool saveSystemState(const RocketSystem &system)
{
    std::ofstream out("nvram.bin", std::ios::binary | std::ios::trunc);
    if(!out) return false;

    // We save a subset of state required for safe recovery.
    out.write(reinterpret_cast<const char*>(&system.current_state), sizeof(State));
    out.write(reinterpret_cast<const char*>(&system.telemetry_sequence), sizeof(unsigned int));
    out.write(reinterpret_cast<const char*>(&system.launch_reference_altitude), sizeof(float));
    out.write(reinterpret_cast<const char*>(&system.payload_deployed), sizeof(bool));
    out.write(reinterpret_cast<const char*>(&system.mission_elapsed_seconds), sizeof(float));

    out.close();
    return true;
}

// Loads the previously saved state from flash memory if available.
// Used during processor boot to check if this is a cold boot or a crash recovery.
bool loadSystemState(RocketSystem &system)
{
    std::ifstream in("nvram.bin", std::ios::binary);
    if(!in) return false;

    in.read(reinterpret_cast<char*>(&system.current_state), sizeof(State));
    in.read(reinterpret_cast<char*>(&system.telemetry_sequence), sizeof(unsigned int));
    in.read(reinterpret_cast<char*>(&system.launch_reference_altitude), sizeof(float));
    in.read(reinterpret_cast<char*>(&system.payload_deployed), sizeof(bool));
    in.read(reinterpret_cast<char*>(&system.mission_elapsed_seconds), sizeof(float));

    in.close();
    return true;
}
