from typing import Optional


class LinkHealth:
    def __init__(self) -> None:
        self.last_quality: Optional[float] = None

    def estimate(self, packet_stats) -> float:
        if packet_stats.total_packets == 0:
            self.last_quality = 0.0
            return 0.0

        quality = 1.0 - max(packet_stats.packet_loss_rate, packet_stats.corruption_rate)
        quality = max(0.0, min(1.0, quality))
        self.last_quality = quality
        return quality

    def human_readable(self, quality: float) -> str:
        if quality >= 0.9:
            return "Excellent"
        if quality >= 0.7:
            return "Good"
        if quality >= 0.4:
            return "Poor"
        return "Critical"
