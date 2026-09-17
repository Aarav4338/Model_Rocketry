#pragma once

// The FSM (src/states.cpp) logs to the console with std::cout/std::endl.
// On desktop that's fine. On the ESP8266 there is no stdout, and pulling in
// the real <iostream> is a well-known trap on this toolchain: libstdc++'s
// iostream construction is heavy (tens of KB of flash + static init cost)
// and some of its code paths assume exception support that Arduino cores are
// commonly built without, which can fail to link or bloat the binary badly.
//
// MISSION_COUT / MISSION_ENDL exist so the exact same call sites in
// states.cpp work on both platforms: on desktop they expand to
// std::cout / std::endl untouched; on Arduino they expand to a tiny
// zero-dependency stream object that forwards to Serial.print().
#ifdef ARDUINO
#include <Arduino.h>

struct MissionStream
{
    MissionStream &operator<<(const char *s) { Serial.print(s); return *this; }
    MissionStream &operator<<(char c) { Serial.print(c); return *this; }
    MissionStream &operator<<(int v) { Serial.print(v); return *this; }
    MissionStream &operator<<(unsigned int v) { Serial.print(v); return *this; }
    MissionStream &operator<<(long v) { Serial.print(v); return *this; }
    MissionStream &operator<<(unsigned long v) { Serial.print(v); return *this; }
    MissionStream &operator<<(float v) { Serial.print(v, 3); return *this; }
    MissionStream &operator<<(double v) { Serial.print(v, 3); return *this; }
    MissionStream &operator<<(bool v) { Serial.print(v ? "true" : "false"); return *this; }
};

extern MissionStream mout;

#define MISSION_COUT mout
#define MISSION_ENDL "\n"
#else
#include <iostream>

#define MISSION_COUT std::cout
#define MISSION_ENDL std::endl
#endif
