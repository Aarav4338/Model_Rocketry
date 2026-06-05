from abc import ABC, abstractmethod
from typing import Optional


class Transport(ABC):
    @abstractmethod
    def open(self) -> None:
        raise NotImplementedError

    @abstractmethod
    def close(self) -> None:
        raise NotImplementedError

    @abstractmethod
    def read_packet(self) -> Optional[str]:
        raise NotImplementedError

    @abstractmethod
    def write_packet(self, packet: str) -> None:
        raise NotImplementedError
