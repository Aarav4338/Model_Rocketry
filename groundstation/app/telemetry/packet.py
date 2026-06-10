from dataclasses import dataclass
from typing import List

from telemetry.schema import FIELD_NAMES_V1, LEGACY_FIELD_NAMES


@dataclass
class TelemetryPacket:
    protocol_version: int
    frame_type: str
    sequence: int
    mission_elapsed_seconds: float
    current_state: str
    current_flight_phase: str
    altitude: float
    vertical_velocity: float
    vertical_acceleration: float
    system_armed: bool
    fault_detected: bool
    descending: bool
    payload_deployed: bool
    pressure: float
    temperature: float
    voltage: float
    gnss_time: int
    gnss_latitude: float
    gnss_longitude: float
    gnss_altitude: float
    gnss_sats: int
    accelerometer_magnitude: float
    gyro_spin_rate: float
    state_name: str
    flight_phase_name: str

    def to_csv_row(self) -> List[str]:
        return [
            str(self.protocol_version),
            self.frame_type,
            str(self.sequence),
            f"{self.mission_elapsed_seconds:.2f}",
            self.current_state,
            self.current_flight_phase,
            f"{self.altitude:.2f}",
            f"{self.vertical_velocity:.2f}",
            f"{self.vertical_acceleration:.2f}",
            "1" if self.system_armed else "0",
            "1" if self.fault_detected else "0",
            "1" if self.descending else "0",
            "1" if self.payload_deployed else "0",
            f"{self.pressure:.2f}",
            f"{self.temperature:.2f}",
            f"{self.voltage:.2f}",
            str(self.gnss_time),
            f"{self.gnss_latitude:.6f}",
            f"{self.gnss_longitude:.6f}",
            f"{self.gnss_altitude:.2f}",
            str(self.gnss_sats),
            f"{self.accelerometer_magnitude:.2f}",
            f"{self.gyro_spin_rate:.2f}",
            self.state_name,
            self.flight_phase_name,
        ]

    @classmethod
    def field_names_v1(cls) -> List[str]:
        return FIELD_NAMES_V1

    @classmethod
    def legacy_field_names(cls) -> List[str]:
        return LEGACY_FIELD_NAMES
