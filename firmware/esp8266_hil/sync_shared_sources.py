"""
Runs automatically before every PlatformIO build (see extra_scripts in
platformio.ini). Copies the shared avionics source files that live at the
repo root (../../include and ../../src) into lib/avionics_core/, using
PlatformIO's standard lib/<name>/{include,src} auto-discovery convention.

This exists so there is exactly ONE copy of the FSM/filters/faults/
telemetry/etc. source to edit -- the root include/ and src/ folders, which
the desktop g++ build also compiles directly. Nothing under lib/ should ever
be hand-edited; it's regenerated on every build and is gitignored.

Excluded on purpose (desktop-only, not portable to the ESP8266):
  - src/main.cpp   (uses <windows.h>, Sleep(), argv[] -- see firmware's own
                     src/main.cpp instead)
  - src/hal.cpp    (the desktop stub HAL -- see firmware's own
                     src/hal_esp8266.cpp instead)
"""
import os
import shutil

Import("env")  # noqa: F821  (PlatformIO injects this at exec time)

PROJECT_DIR = env["PROJECT_DIR"]
REPO_ROOT = os.path.abspath(os.path.join(PROJECT_DIR, "..", ".."))

SRC_ROOT = os.path.join(REPO_ROOT, "src")
INCLUDE_ROOT = os.path.join(REPO_ROOT, "include")

LIB_DIR = os.path.join(PROJECT_DIR, "lib", "avionics_core")
LIB_SRC = os.path.join(LIB_DIR, "src")
LIB_INCLUDE = os.path.join(LIB_DIR, "include")

EXCLUDED_SRC_FILES = {"main.cpp", "hal.cpp"}


def sync_dir(src_dir, dst_dir, excluded_files):
    if os.path.isdir(dst_dir):
        shutil.rmtree(dst_dir)
    os.makedirs(dst_dir, exist_ok=True)

    if not os.path.isdir(src_dir):
        print(f"[sync_shared_sources] WARNING: {src_dir} not found, skipping")
        return

    for name in os.listdir(src_dir):
        if name in excluded_files:
            continue
        src_path = os.path.join(src_dir, name)
        if os.path.isfile(src_path):
            shutil.copy2(src_path, os.path.join(dst_dir, name))


sync_dir(SRC_ROOT, LIB_SRC, EXCLUDED_SRC_FILES)
sync_dir(INCLUDE_ROOT, LIB_INCLUDE, set())

print(f"[sync_shared_sources] Synced shared FSM sources from {REPO_ROOT} into {LIB_DIR}")
