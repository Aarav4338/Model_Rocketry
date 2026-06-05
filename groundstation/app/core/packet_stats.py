from dataclasses import dataclass, field
from datetime import datetime
from typing import List, Optional


@dataclass
class PacketStats:
    total_packets: int = 0
    rejected_packets: int = 0
    checksum_failures: int = 0
    corrupted_packets: int = 0
    lost_packets: int = 0
    first_packet_time: Optional[datetime] = None
    last_packet_time: Optional[datetime] = None
    last_sequence: Optional[int] = None
    received_sequences: List[int] = field(default_factory=list)

    def record_valid_packet(self, sequence: int, timestamp: datetime) -> None:
        self.total_packets += 1
        self.received_sequences.append(sequence)
        self.last_packet_time = timestamp
        if self.first_packet_time is None:
            self.first_packet_time = timestamp

        if self.last_sequence is not None and sequence > self.last_sequence + 1:
            self.lost_packets += sequence - self.last_sequence - 1

        self.last_sequence = sequence

    def record_rejected_packet(self, checksum_failure: bool = False) -> None:
        self.rejected_packets += 1
        self.corrupted_packets += 1
        if checksum_failure:
            self.checksum_failures += 1

    @property
    def duration_seconds(self) -> float:
        if self.first_packet_time is None or self.last_packet_time is None:
            return 0.0
        return (self.last_packet_time - self.first_packet_time).total_seconds()

    @property
    def average_frequency_hz(self) -> float:
        if self.duration_seconds <= 0.0:
            return 0.0
        return self.total_packets / self.duration_seconds

    @property
    def packet_loss_rate(self) -> float:
        expected = self.total_packets + self.lost_packets
        if expected <= 0:
            return 0.0
        return self.lost_packets / expected

    @property
    def corruption_rate(self) -> float:
        total = self.total_packets + self.corrupted_packets
        if total == 0:
            return 0.0
        return self.corrupted_packets / total
