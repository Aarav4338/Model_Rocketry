import json
import os
from datetime import datetime
from typing import Any, Dict, List


class DiagnosticsLogger:
    def __init__(self, output_path: str) -> None:
        self.output_path = output_path
        self._handle = None

    def open(self) -> None:
        os.makedirs(os.path.dirname(self.output_path) or ".", exist_ok=True)
        self._handle = open(self.output_path, "a", encoding="utf-8")

    def close(self) -> None:
        if self._handle is None:
            return
        self._handle.close()
        self._handle = None

    def log_corruption(self, raw_line: str, errors: List[str]) -> None:
        if self._handle is None:
            raise RuntimeError("Diagnostics logger is not open")
        timestamp = datetime.utcnow().isoformat(timespec="seconds") + "Z"
        self._handle.write(f"[{timestamp}] CORRUPTED PACKET: {raw_line}\n")
        for error in errors:
            self._handle.write(f"    - {error}\n")
        self._handle.flush()

    def log_info(self, message: str) -> None:
        if self._handle is None:
            raise RuntimeError("Diagnostics logger is not open")
        timestamp = datetime.utcnow().isoformat(timespec="seconds") + "Z"
        self._handle.write(f"[{timestamp}] INFO: {message}\n")
        self._handle.flush()

    def log_summary(self, metrics: Dict[str, Any]) -> None:
        if self._handle is None:
            raise RuntimeError("Diagnostics logger is not open")
        timestamp = datetime.utcnow().isoformat(timespec="seconds") + "Z"
        payload = json.dumps(metrics, sort_keys=True)
        self._handle.write(f"[{timestamp}] METRICS: {payload}\n")
        self._handle.flush()
