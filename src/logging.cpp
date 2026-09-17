#include "logging.hpp"

#include <cstdio>
#include <cstring>
#ifdef ARDUINO
#include <EEPROM.h>
#else
#include <fstream>
#endif
#include "hal.hpp"

// Layout of the crash-recovery blob saved to non-volatile storage. Both the
// desktop (nvram.bin file) and ESP8266 (emulated EEPROM flash sector) paths
// save exactly this set of fields, in this order, so the two implementations
// stay interchangeable.
namespace
{
    struct NvramBlob
    {
        State current_state;
        unsigned int telemetry_sequence;
        float launch_reference_altitude;
        bool payload_deployed;
        float mission_elapsed_seconds;
    };
}

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

// Appends one formatted line to the SD-card or flash log via HAL.
static void appendMissionLogLine(RocketSystem &system,
                                 const char *line)
{
    (void)system; // Unused now, but kept for signature consistency if desired
    
    // Write directly to the SD Card
    Hardware::writeToSDCard(line, std::strlen(line));
    Hardware::writeToSDCard("\n", 1);
    Hardware::flushSDCard();
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

#ifdef ARDUINO
// ESP8266 EEPROM.h emulates byte-addressable EEPROM inside a reserved flash
// sector. EEPROM.begin(size) copies that sector into a RAM buffer; writes
// only hit flash once EEPROM.commit() is called, so a genuine mid-flight
// brownout can still lose the last unsaved write — same real-world caveat a
// crash-recovery blob would have on any MCU without a battery-backed RTC RAM.
// A magic byte at offset 0 distinguishes "never written" flash (reads as
// 0xFF) from a real saved blob, mirroring loadSystemState()'s std::ifstream
// "file doesn't exist" check on desktop.
namespace
{
    constexpr int NVRAM_EEPROM_SIZE = sizeof(NvramBlob) + 1;
    constexpr uint8_t NVRAM_MAGIC = 0xA5;
}
#endif

// Simulates dumping critical volatile RAM into an EEPROM or Flash sector.
// Used for telemetry backup and data recovery strategy after a crash.
bool saveSystemState(const RocketSystem &system)
{
    NvramBlob blob;
    blob.current_state = system.current_state;
    blob.telemetry_sequence = system.telemetry_sequence;
    blob.launch_reference_altitude = system.launch_reference_altitude;
    blob.payload_deployed = system.payload_deployed;
    blob.mission_elapsed_seconds = system.mission_elapsed_seconds;

#ifdef ARDUINO
    EEPROM.begin(NVRAM_EEPROM_SIZE);
    EEPROM.write(0, NVRAM_MAGIC);
    EEPROM.put(1, blob);
    bool ok = EEPROM.commit();
    EEPROM.end();
    return ok;
#else
    std::ofstream out("nvram.bin", std::ios::binary | std::ios::trunc);
    if(!out) return false;

    out.write(reinterpret_cast<const char*>(&blob), sizeof(NvramBlob));

    out.close();
    return true;
#endif
}

// Loads the previously saved state from flash memory if available.
// Used during processor boot to check if this is a cold boot or a crash recovery.
bool loadSystemState(RocketSystem &system)
{
    NvramBlob blob;

#ifdef ARDUINO
    EEPROM.begin(NVRAM_EEPROM_SIZE);
    uint8_t magic = EEPROM.read(0);
    if (magic != NVRAM_MAGIC)
    {
        EEPROM.end();
        return false; // flash sector never written — cold boot
    }
    EEPROM.get(1, blob);
    EEPROM.end();
#else
    std::ifstream in("nvram.bin", std::ios::binary);
    if(!in) return false;

    in.read(reinterpret_cast<char*>(&blob), sizeof(NvramBlob));

    in.close();
#endif

    system.current_state = blob.current_state;
    system.telemetry_sequence = blob.telemetry_sequence;
    system.launch_reference_altitude = blob.launch_reference_altitude;
    system.payload_deployed = blob.payload_deployed;
    system.mission_elapsed_seconds = blob.mission_elapsed_seconds;

    return true;
}
