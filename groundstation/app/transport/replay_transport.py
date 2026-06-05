from pathlib import Path
from typing import List, Optional

from transport.transport import Transport


class ReplayTransport(Transport):
    def __init__(self, replay_path: str) -> None:
        self.replay_path = replay_path
        self._open = False
        self._packets: List[str] = []
        self._position = 0

    def open(self) -> None:
        self._open = True
        replay_file = Path(self.replay_path)
        if not replay_file.exists():
            self._packets = []
            return

        with replay_file.open("r", encoding="utf-8") as handle:
            self._packets = [line.strip() for line in handle if line.strip()]
        self._position = 0

    def close(self) -> None:
        self._open = False
        self._packets = []
        self._position = 0

    def read_packet(self) -> Optional[str]:
        if not self._open or self._position >= len(self._packets):
            return None
        packet = self._packets[self._position]
        self._position += 1
        return packet

    def write_packet(self, packet: str) -> None:
        raise NotImplementedError("Replay transport does not support uplink commands")
