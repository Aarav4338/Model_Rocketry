"""QThread worker that polls TelemetryManager and emits signals to the UI.

This is the ONLY bridge between the backend pipeline and the Qt event loop.
All backend logic stays in TelemetryManager — the worker just calls run_once()
and forwards results via thread-safe Qt signals.

V1.5: Added smart event detection with hysteresis for state transitions,
apogee detection, link degradation, fault detection, and packet loss.
"""

import time as _time

from PyQt6.QtCore import QThread, pyqtSignal

from core.telemetry_manager import TelemetryManager


class TelemetryWorker(QThread):
    """Background thread that drives the TelemetryManager polling loop.

    Signals
    -------
    packet_received : object
        Emitted when a valid TelemetryPacket is produced by the backend.
    stats_updated : dict
        Emitted after every run_once() that handled a packet (valid or rejected).
        Carries the metrics dict from TelemetryManager._metrics().
    log_message : str, str
        Emitted for events the mission log should display.
        Arguments: (message, level) where level is INFO/WARN/ERROR/EVENT.
    """

    packet_received = pyqtSignal(object)
    stats_updated = pyqtSignal(dict)
    log_message = pyqtSignal(str, str)  # message, level

    def __init__(self, manager: TelemetryManager, poll_interval: float = 0.25,
                 parent=None) -> None:
        super().__init__(parent)
        self.manager = manager
        self.poll_interval_ms = int(poll_interval * 1000)
        self._stop_requested = False

        # --- State tracking for smart event detection ---
        self._last_state_name: str | None = None
        self._last_velocity_sign: float | None = None  # >0, <=0
        self._apogee_logged: bool = False
        self._descent_logged: bool = False
        self._fault_logged: bool = False
        self._last_link_tier: int = 2  # 0=poor, 1=fair, 2=good
        self._last_lost_count: int = 0
        self._accumulated_loss: int = 0
        self._loss_burst_start: float = 0.0

    def run(self) -> None:
        """Main worker loop — runs on the background thread."""
        self.manager.start()
        self.log_message.emit("Backend started — listening for telemetry", "EVENT")

        try:
            while not self._stop_requested:
                handled = self.manager.run_once()

                if handled:
                    packet = self.manager.last_packet
                    metrics = self.manager._metrics()

                    if packet is not None:
                        self._detect_events(packet, metrics)
                        self.packet_received.emit(packet)

                    # Stats are updated for both valid and rejected packets
                    self.stats_updated.emit(metrics)

                    # Log rejected packets (packet is None when rejected)
                    if packet is None:
                        rejected = metrics.get("rejected_packets", "?")
                        self.log_message.emit(
                            f"Packet rejected (total: {rejected})",
                            "WARN",
                        )
                else:
                    self.msleep(self.poll_interval_ms)

        except Exception as exc:
            self.log_message.emit(f"Worker error: {exc}", "ERROR")
        finally:
            self.manager.stop()
            self.log_message.emit("Backend stopped", "EVENT")

    def request_stop(self) -> None:
        """Request the worker to stop after the current iteration."""
        self._stop_requested = True

    # ------------------------------------------------------------------
    # Smart event detection
    # ------------------------------------------------------------------

    def _detect_events(self, packet, metrics: dict) -> None:
        """Check for meaningful events and emit log messages.

        All detection is simple scalar comparisons — no backend calls.
        Hysteresis and one-shot flags prevent log spam.
        """
        met = packet.mission_elapsed_seconds
        prefix = f"[T+{met:.1f}s]"

        # --- State transition ---
        if self._last_state_name is not None and \
                packet.state_name != self._last_state_name:
            self.log_message.emit(
                f"{prefix} STATE → {packet.state_name}",
                "EVENT",
            )
        self._last_state_name = packet.state_name

        # --- Apogee detection (velocity crosses from positive to ≤ 0) ---
        if not self._apogee_logged:
            vel = packet.vertical_velocity
            if self._last_velocity_sign is not None:
                if self._last_velocity_sign > 0 and vel <= 0:
                    self._apogee_logged = True
                    self.log_message.emit(
                        f"{prefix} APOGEE DETECTED — alt {packet.altitude:.1f} m",
                        "EVENT",
                    )
            self._last_velocity_sign = vel

        # --- Descent detected ---
        if not self._descent_logged and packet.descending:
            self._descent_logged = True
            self.log_message.emit(
                f"{prefix} DESCENT DETECTED",
                "EVENT",
            )

        # --- Fault detected ---
        if not self._fault_logged and packet.fault_detected:
            self._fault_logged = True
            self.log_message.emit(
                f"{prefix} FAULT DETECTED",
                "ERROR",
            )

        # --- Link quality tier transitions (hysteresis) ---
        link_quality = metrics.get("link_quality", 1.0)
        current_tier = self._last_link_tier
        
        if self._last_link_tier == 2:
            if link_quality < 0.65: current_tier = 1
            if link_quality < 0.35: current_tier = 0
        elif self._last_link_tier == 1:
            if link_quality > 0.75: current_tier = 2
            if link_quality < 0.35: current_tier = 0
        elif self._last_link_tier == 0:
            if link_quality > 0.75: current_tier = 2
            elif link_quality > 0.45: current_tier = 1

        if current_tier < self._last_link_tier:
            # Degraded
            pct = link_quality * 100
            if current_tier == 0:
                self.log_message.emit(
                    f"{prefix} LINK POOR ({pct:.0f}%)",
                    "WARN",
                )
            else:
                self.log_message.emit(
                    f"{prefix} LINK DEGRADED ({pct:.0f}%)",
                    "WARN",
                )
        elif current_tier > self._last_link_tier and self._last_link_tier < 2:
            # Recovered
            pct = link_quality * 100
            self.log_message.emit(
                f"{prefix} LINK RECOVERED ({pct:.0f}%)",
                "EVENT",
            )
        self._last_link_tier = current_tier

        # --- Packet loss spike (aggregation over 5 seconds) ---
        lost = metrics.get("lost_packets", 0)
        now = _time.monotonic()
        if lost > self._last_lost_count:
            self._accumulated_loss += (lost - self._last_lost_count)
            self._last_lost_count = lost
            if self._loss_burst_start == 0.0:
                self._loss_burst_start = now

        if self._accumulated_loss > 0 and (now - self._loss_burst_start) >= 5.0:
            self.log_message.emit(
                f"{prefix} LINK INSTABILITY DETECTED (Accumulated loss: +{self._accumulated_loss})",
                "WARN",
            )
            self._accumulated_loss = 0
            self._loss_burst_start = 0.0


