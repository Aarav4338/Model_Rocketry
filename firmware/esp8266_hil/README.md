# ESP8266 Hardware-in-the-Loop (HIL) Bench Build

This is "Option 1" from the earlier discussion: run the exact same flight
computer FSM that the desktop simulator validates, on the actual ESP8266 +
sensors, with a **fabricated flight** driving the state machine so the real
IMU/barometer/GPS can sit still on the bench. Everything else is real: the
LoRa radio actually transmits, the servo actually fires on deployment, the
watchdog is the ESP8266's real one, and crash-recovery state is really
written to flash (EEPROM-emulated).

## What this proves (and what it doesn't)

This build exercises real timing, real SPI/I2C bus behavior, the real radio
link to your ground station, real actuation, and the real watchdog/crash-
recovery path — none of which the desktop simulator can touch, since it has
no hardware at all. It does **not** validate the physical flight itself:
motor thrust, aerodynamics, parachute deployment mechanics, or the actual
sensor readings under real flight loads. That's what flipping
`FlightConfig::HIL_MODE` to `false` (config.hpp) and doing a real or
physically-excited-sensor test is for, later.

## Parts assumed

- ESP8266 devkit (NodeMCU/Wemos-style, with the labeled D0-D8/RX/TX/A0 pins)
- MPU6050 6-axis IMU (I2C)
- BMP280 barometer (I2C)
- NEO-6M GPS module (UART)
- LoRa 433MHz module, SX1278/RA-02 style (SPI)
- 1x servo (payload deployment actuator, standing in for a real release
  mechanism/pyro channel)
- DHT11 — **listed in the parts but not wired in this build.** See
  `include/hil_pins.hpp` for why: a bare ESP8266 doesn't have a free GPIO
  left once the five flight-relevant modules above are connected. It's
  bonus ambient telemetry, not part of the FSM's flight-safety logic, so it
  was left out rather than forcing a fragile pin-sharing hack. Options if
  you want it too: free up a pin by dropping something else, add an I2C
  GPIO expander (e.g. PCF8574), or move the real board to an ESP32 (much
  more GPIO headroom — a natural upgrade path anyway once you add a real SD
  card and a second barometer for genuine sensor redundancy).

## Wiring

See `include/hil_pins.hpp` for the full pin map and the caveats on each
pin (boot-strapping pins, the D8 pull-down, the A0 divider range). **This
was written without access to your actual board — verify every pin against
your specific devkit's silkscreen before connecting anything.**

## Building and flashing

This needs [PlatformIO](https://platformio.org/) (the CLI or the VS Code
extension). From this folder:

```bash
pio run                 # build
pio run --target upload # build + flash over USB
pio device monitor      # watch the serial console (115200 baud)
```

The `lib/avionics_core/` folder is generated automatically before every
build by `sync_shared_sources.py` — it copies the shared FSM/filters/
faults/telemetry/etc. sources from the repo root's `include/` and `src/`
(everything except `main.cpp` and `hal.cpp`, which are desktop-only). **Edit
the root `include/` and `src/` files, never anything under `lib/`** — it's
gitignored and gets wiped and regenerated on the next build anyway. This is
what keeps the desktop simulator and this firmware running the exact same
flight logic from one source.

## Choosing a scenario

There's no command line on a microcontroller, so unlike
`./rocket-avionics <scenario>` on desktop, the scenario is chosen at compile
time: `FlightConfig::HIL_SCENARIO` in `include/config.hpp`. Change the
number, reflash. 0=success, 1=motor failure, 2=sensor failure, 3=parachute
failure, 4=MCU reset test.

Once a mission completes, this build auto-restarts it after 8 seconds
(see the comment in `src/main.cpp`) so you can watch repeated bench runs
without needing to physically reset the board. Comment that out once you're
past bench-testing.

## Switching out of HIL mode later

`FlightConfig::HIL_MODE` (config.hpp) is the single switch. Set it `false`
once you want the FSM driven by the real, physically-stationary (or later,
actually flying) sensors instead of the fabricated flight — that path is
`src/real_sensors.cpp`, which converts real barometer pressure and real IMU
readings into the same fields the FSM already reads.

## Known gaps / things to verify before this flies for real

- No real SD card in the current parts list — `Hardware::writeToSDCard()`
  writes to LittleFS on the ESP8266's own flash instead (see the comment in
  `src/hal_esp8266.cpp`). Fine for a bench log; add a real SD module before
  a real flight if you need removable storage.
- Only one BMP280 — `readRedundantBarometer()` currently just reads the
  same physical sensor twice. Add a second barometer for genuine hardware
  redundancy.
- The battery ADC divider ratio (`FlightConfig::HIL_BATTERY_DIVIDER_RATIO`)
  is a placeholder — calibrate it against a multimeter reading of your
  actual pack before trusting `readBatteryVoltage()`.
- No CAN transceiver in the parts list — `Hardware::initCAN()`/
  `transmitCANPayload()` are stubbed to `false`/no-op. Only matters once
  you're integrating the competition's CAN-7U payload interface.
