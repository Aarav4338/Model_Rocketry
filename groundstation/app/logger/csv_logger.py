import csv
import os
from typing import Optional

from telemetry.packet import TelemetryPacket


class CsvLogger:
    def __init__(self, output_path: str) -> None:
        self.output_path = output_path
        self._handle = None
        self._writer = None

    def open(self) -> None:
        os.makedirs(os.path.dirname(self.output_path) or ".", exist_ok=True)
        is_new = not os.path.exists(self.output_path)
        self._handle = open(self.output_path, "a", newline="", encoding="utf-8")
        self._writer = csv.writer(self._handle)
        if is_new:
            self._writer.writerow(["protocol_version", "frame_type", "sequence", "mission_elapsed_seconds", "current_state", "current_flight_phase", "altitude", "vertical_velocity", "vertical_acceleration", "system_armed", "fault_detected", "descending", "payload_deployed", "pressure", "temperature", "voltage", "gnss_time", "gnss_latitude", "gnss_longitude", "gnss_altitude", "gnss_sats", "accelerometer_magnitude", "gyro_spin_rate", "state_name", "flight_phase_name"])

    def close(self) -> None:
        if self._handle is not None:
            self._handle.close()
            self._handle = None
            self._writer = None

    def log_packet(self, packet: TelemetryPacket) -> None:
        if self._writer is None:
            raise RuntimeError("CSV logger is not open")
        self._writer.writerow(packet.to_csv_row())
        self._handle.flush()
