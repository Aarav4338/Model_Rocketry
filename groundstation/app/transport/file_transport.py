import os
from typing import Optional

from transport.transport import Transport


class FileTransport(Transport):
    def __init__(self, input_path: str, command_path: str) -> None:
        self.input_path = input_path
        self.command_path = command_path
        self._open = False

    def open(self) -> None:
        self._open = True
        os.makedirs(os.path.dirname(self.input_path) or ".", exist_ok=True)
        command_dir = os.path.dirname(self.command_path)
        if command_dir:
            os.makedirs(command_dir, exist_ok=True)

    def close(self) -> None:
        self._open = False

    def read_packet(self) -> Optional[str]:
        if not self._open:
            return None
        if not os.path.exists(self.input_path):
            return None

        with open(self.input_path, "r", encoding="utf-8") as handle:
            lines = [line.rstrip("\n") for line in handle.readlines()]

        if not lines:
            return None

        packet = None
        rest = []
        for line in lines:
            stripped = line.strip()
            if packet is None and stripped:
                packet = stripped
                continue
            rest.append(line)

        with open(self.input_path, "w", encoding="utf-8") as handle:
            for line in rest:
                handle.write(f"{line}\n")

        return packet

    def write_packet(self, packet: str) -> None:
        if not self._open:
            return
        with open(self.command_path, "a", encoding="utf-8") as handle:
            handle.write(f"{packet}\n")
