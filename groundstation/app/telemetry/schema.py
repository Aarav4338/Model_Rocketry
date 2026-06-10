from typing import List

PROTOCOL_VERSION_V1 = 1
FRAME_TYPE_TELEMETRY = "TELEMETRY"

# Canonical state and phase tokens (short machine-friendly)
STATE_ASCENT = "ASCENT"
STATE_DESCENT = "DESCENT"
STATE_COAST = "COAST"

PHASE_COAST = "COAST"
PHASE_ASCENT = "ASCENT"
PHASE_DESCENT = "DESCENT"

# Field order for v1 telemetry packets, excluding checksum.
FIELD_NAMES_V1: List[str] = [
    "protocol_version",
    "frame_type",
    "sequence",
    "mission_elapsed_seconds",
    "current_state",
    "current_flight_phase",
    "altitude",
    "vertical_velocity",
    "vertical_acceleration",
    "system_armed",
    "fault_detected",
    "descending",
    "payload_deployed",
    "pressure",
    "temperature",
    "voltage",
    "gnss_time",
    "gnss_latitude",
    "gnss_longitude",
    "gnss_altitude",
    "gnss_sats",
    "accelerometer_magnitude",
    "gyro_spin_rate",
    "state_name",
    "flight_phase_name",
]

LEGACY_FIELD_NAMES: List[str] = [
    "sequence",
    "mission_elapsed_seconds",
    "current_state",
    "current_flight_phase",
    "altitude",
    "vertical_velocity",
    "vertical_acceleration",
    "system_armed",
    "fault_detected",
    "descending",
    "payload_deployed",
    "pressure",
    "temperature",
    "voltage",
    "gnss_time",
    "gnss_latitude",
    "gnss_longitude",
    "gnss_altitude",
    "gnss_sats",
    "accelerometer_magnitude",
    "gyro_spin_rate",
    "state_name",
    "flight_phase_name",
]

EXPECTED_FIELD_COUNT_V1 = len(FIELD_NAMES_V1) + 1  # +checksum
EXPECTED_FIELD_COUNT_LEGACY = len(LEGACY_FIELD_NAMES) + 1  # +checksum
