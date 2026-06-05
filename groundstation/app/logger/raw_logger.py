import csv
import os
from datetime import datetime


class RawTelemetryLogger:
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
            self._writer.writerow(["timestamp", "raw_packet"])

    def close(self) -> None:
        if self._handle is not None:
            self._handle.close()
            self._handle = None
            self._writer = None

    def log_raw_packet(self, raw_packet: str) -> None:
        if self._writer is None:
            raise RuntimeError("Raw telemetry logger is not open")
        timestamp = datetime.utcnow().isoformat(timespec="seconds") + "Z"
        self._writer.writerow([timestamp, raw_packet])
        self._handle.flush()
