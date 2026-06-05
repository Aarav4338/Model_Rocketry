import os
from datetime import datetime
from typing import List


class RejectedPacketLogger:
    def __init__(self, output_path: str) -> None:
        self.output_path = output_path
        self._handle = None

    def open(self) -> None:
        os.makedirs(os.path.dirname(self.output_path) or ".", exist_ok=True)
        self._handle = open(self.output_path, "a", encoding="utf-8")

    def close(self) -> None:
        if self._handle is not None:
            self._handle.close()
            self._handle = None

    def log_rejection(self, raw_packet: str, errors: List[str]) -> None:
        if self._handle is None:
            raise RuntimeError("Rejected packet logger is not open")
        timestamp = datetime.utcnow().isoformat(timespec="seconds") + "Z"
        error_text = "; ".join(errors)
        self._handle.write(f"[{timestamp}] REJECTED: {error_text} | RAW: {raw_packet}\n")
        self._handle.flush()
