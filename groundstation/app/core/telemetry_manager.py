import time
from datetime import datetime
from typing import Optional

from logger.csv_logger import CsvLogger
from logger.raw_logger import RawTelemetryLogger
from logger.rejected_logger import RejectedPacketLogger
from parser.telemetry_parser import TelemetryParser
from transport.transport import Transport
from validation.packet_validator import PacketValidator
from core.link_health import LinkHealth
from core.packet_stats import PacketStats
from telemetry.packet import TelemetryPacket
from logger.diagnostics import DiagnosticsLogger


class TelemetryManager:
    def __init__(
        self,
        transport: Transport,
        validator: PacketValidator,
        parser: TelemetryParser,
        logger: CsvLogger,
        diagnostics: DiagnosticsLogger,
        raw_logger: RawTelemetryLogger,
        rejected_logger: RejectedPacketLogger,
    ) -> None:
        self.transport = transport
        self.validator = validator
        self.parser = parser
        self.logger = logger
        self.diagnostics = diagnostics
        self.raw_logger = raw_logger
        self.rejected_logger = rejected_logger
        self.stats = PacketStats()
        self.link_health = LinkHealth()
        self.last_packet: Optional[TelemetryPacket] = None

    def start(self) -> None:
        self.transport.open()
        self.logger.open()
        self.diagnostics.open()
        self.raw_logger.open()
        self.rejected_logger.open()

    def stop(self) -> None:
        self.transport.close()
        self.logger.close()
        self.diagnostics.close()
        self.raw_logger.close()
        self.rejected_logger.close()

    def run_once(self) -> bool:
        raw_packet = self.transport.read_packet()
        if raw_packet is None:
            return False

        self.raw_logger.log_raw_packet(raw_packet)
        timestamp = datetime.utcnow()
        validation = self.validator.validate(raw_packet)
        if not validation.valid:
            checksum_failure = any(
                error.lower().startswith("checksum") for error in validation.errors
            )
            self.stats.record_rejected_packet(checksum_failure=checksum_failure)
            self.rejected_logger.log_rejection(raw_packet, validation.errors)
            self.diagnostics.log_corruption(raw_packet, validation.errors)
            self.diagnostics.log_summary(self._metrics())
            return True

        try:
            packet = self.parser.parse(validation.fields, validation.version)
        except ValueError as exc:
            self.stats.record_rejected_packet()
            self.rejected_logger.log_rejection(raw_packet, [str(exc)])
            self.diagnostics.log_corruption(raw_packet, [str(exc)])
            self.diagnostics.log_summary(self._metrics())
            return True

        self.stats.record_valid_packet(packet.sequence, timestamp)
        self.logger.log_packet(packet)
        self.last_packet = packet
        self.diagnostics.log_summary(self._metrics())
        return True

    def report(self) -> str:
        quality = self.link_health.estimate(self.stats)
        last_received = (
            self.stats.last_packet_time.isoformat(timespec="seconds") + "Z"
            if self.stats.last_packet_time
            else "N/A"
        )
        return (
            f"Packets: {self.stats.total_packets}, "
            f"Rejected: {self.stats.rejected_packets}, "
            f"Checksum failures: {self.stats.checksum_failures}, "
            f"Lost: {self.stats.lost_packets}, "
            f"Frequency: {self.stats.average_frequency_hz:.2f} Hz, "
            f"Last packet: {last_received}, "
            f"Link Quality: {quality:.2f} ({self.link_health.human_readable(quality)})"
        )

    def pretty_print(self) -> None:
        if self.last_packet is None:
            print("No valid telemetry received yet.")
            print(self.report())
            return

        packet = self.last_packet
        print("--- TELEMETRY ---")
        print(f"seq={packet.sequence} time={packet.mission_elapsed_seconds:.2f}s state={packet.state_name} phase={packet.flight_phase_name}")
        print(f"alt={packet.altitude:.2f} m vel={packet.vertical_velocity:.2f} m/s accel={packet.vertical_acceleration:.2f} m/s^2")
        print(f"pressure={packet.pressure:.1f} Pa temp={packet.temperature:.1f} °C volt={packet.voltage:.2f} V")
        print(f"gps={packet.gnss_latitude:.6f},{packet.gnss_longitude:.6f} alt={packet.gnss_altitude:.2f} sats={packet.gnss_sats}")
        print(f"armed={packet.system_armed} fault={packet.fault_detected} descending={packet.descending} payload={packet.payload_deployed}")
        print(self.report())

    def loop(self, poll_interval: float = 0.25) -> None:
        self.start()
        try:
            while True:
                handled = self.run_once()
                if handled:
                    self.pretty_print()
                else:
                    time.sleep(poll_interval)
        except KeyboardInterrupt:
            print("Telemetry manager stopped by user.")
        finally:
            self.stop()

    def _metrics(self) -> dict:
        quality = self.link_health.estimate(self.stats)
        return {
            "total_packets": self.stats.total_packets,
            "rejected_packets": self.stats.rejected_packets,
            "checksum_failures": self.stats.checksum_failures,
            "lost_packets": self.stats.lost_packets,
            "frequency_hz": round(self.stats.average_frequency_hz, 3),
            "corruption_rate": round(self.stats.corruption_rate, 3),
            "link_quality": round(quality, 3),
            "last_packet_time": self.stats.last_packet_time.isoformat(timespec="seconds") + "Z" if self.stats.last_packet_time else None,
        }
