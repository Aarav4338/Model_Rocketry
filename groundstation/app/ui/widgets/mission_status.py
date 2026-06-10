"""Mission Status panel — left-side widget showing current mission state.

Displays a prominent hero state indicator, flight phase, mission elapsed
time, link quality with human-readable label, packet rate, checksum
failure count, and a backend heartbeat dot.
"""

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtWidgets import QFrame, QGridLayout, QHBoxLayout, QLabel, QVBoxLayout

from ui.theme import (
    BG_PANEL,
    BORDER,
    LINK_CRITICAL,
    LINK_EXCELLENT,
    LINK_GOOD,
    LINK_POOR,
    STATUS_CRITICAL,
    STATUS_INACTIVE,
    STATUS_NOMINAL,
    STATUS_WARNING,
    TEXT_PRIMARY,
    TEXT_SECONDARY,
    font_section_title,
    font_state_hero,
    font_status_label,
    font_status_value,
)

_DEFAULT_VALUE = "---"

# States mapped to their nominal status color
_STATE_COLORS = {
    "Ascent": STATUS_NOMINAL,
    "Landed": STATUS_NOMINAL,
    "Coast": STATUS_WARNING,
    "Descent": STATUS_WARNING,
    "Boot": STATUS_INACTIVE,
}

# Heartbeat timeout in milliseconds
_HEARTBEAT_TIMEOUT_MS = 2000


class MissionStatusWidget(QFrame):
    """Left-panel widget displaying current mission state information."""

    def __init__(self, parent=None):
        super().__init__(parent)

        self.setFixedWidth(280)

        # Styling — dark panel with rounded corners and thin border
        self.setStyleSheet(
            f"MissionStatusWidget {{"
            f"  background-color: {BG_PANEL.name()};"
            f"  border: 1px solid {BORDER.name()};"
            f"  border-radius: 6px;"
            f"}}"
        )

        # --- Main vertical layout ---
        root_layout = QVBoxLayout(self)
        root_layout.setContentsMargins(14, 12, 14, 12)
        root_layout.setSpacing(8)

        # --- Title row with heartbeat dot ---
        title_row = QHBoxLayout()
        title_row.setSpacing(8)

        title = QLabel("MISSION STATUS")
        title.setFont(font_section_title())
        title.setStyleSheet(f"color: {TEXT_PRIMARY.name()}; border: none;")
        title.setAlignment(Qt.AlignmentFlag.AlignLeft)
        title_row.addWidget(title)

        title_row.addStretch()

        self._heartbeat_dot = QLabel("●")
        self._heartbeat_dot.setFont(font_status_label())
        self._heartbeat_dot.setFixedWidth(16)
        self._heartbeat_dot.setStyleSheet(f"color: {STATUS_INACTIVE.name()}; border: none;")
        self._heartbeat_dot.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._heartbeat_dot.setToolTip("Backend heartbeat")
        title_row.addWidget(self._heartbeat_dot)

        root_layout.addLayout(title_row)

        # --- Hero state block ---
        self._state_hero = QLabel(_DEFAULT_VALUE)
        self._state_hero.setFont(font_state_hero())
        self._state_hero.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._state_hero.setStyleSheet(
            f"color: {STATUS_INACTIVE.name()}; border: none; padding: 8px 0;"
        )
        root_layout.addWidget(self._state_hero)

        # --- Grid for secondary status rows ---
        grid = QGridLayout()
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(8)
        grid.setVerticalSpacing(6)
        grid.setColumnStretch(1, 1)

        # Row 0 — PHASE
        row = 0
        grid.addWidget(self._make_label("PHASE"), row, 0, Qt.AlignmentFlag.AlignLeft)
        self._phase_value = self._make_value()
        grid.addWidget(self._phase_value, row, 1, Qt.AlignmentFlag.AlignRight)

        # Row 1 — MISSION TIME
        row = 1
        grid.addWidget(self._make_label("MISSION TIME"), row, 0, Qt.AlignmentFlag.AlignLeft)
        self._time_value = self._make_value()
        grid.addWidget(self._time_value, row, 1, Qt.AlignmentFlag.AlignRight)

        # Row 2 — LINK QUALITY
        row = 2
        grid.addWidget(self._make_label("LINK QUALITY"), row, 0, Qt.AlignmentFlag.AlignLeft)
        self._link_value = self._make_value()
        grid.addWidget(self._link_value, row, 1, Qt.AlignmentFlag.AlignRight)

        # Row 3 — PACKET RATE
        row = 3
        grid.addWidget(self._make_label("PACKET RATE"), row, 0, Qt.AlignmentFlag.AlignLeft)
        self._rate_value = self._make_value()
        grid.addWidget(self._rate_value, row, 1, Qt.AlignmentFlag.AlignRight)

        # Row 4 — CORRUPTED
        row = 4
        grid.addWidget(self._make_label("CORRUPTED"), row, 0, Qt.AlignmentFlag.AlignLeft)
        self._corrupt_value = self._make_value()
        grid.addWidget(self._corrupt_value, row, 1, Qt.AlignmentFlag.AlignRight)

        root_layout.addLayout(grid)
        root_layout.addStretch()

        # --- Heartbeat timer ---
        self._heartbeat_timer = QTimer(self)
        self._heartbeat_timer.setInterval(_HEARTBEAT_TIMEOUT_MS)
        self._heartbeat_timer.setSingleShot(True)
        self._heartbeat_timer.timeout.connect(self._on_heartbeat_timeout)
        self._heartbeat_active = False

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def update_status(self, packet, metrics: dict) -> None:
        """Refresh all displayed fields from *packet* and *metrics*."""
        # --- HERO STATE ---
        state_color = self._resolve_state_color(packet)
        self._state_hero.setText(f"● {packet.state_name.upper()}")
        self._state_hero.setStyleSheet(
            f"color: {state_color.name()}; border: none; padding: 8px 0;"
        )

        # --- PHASE ---
        self._phase_value.setText(packet.flight_phase_name)

        # --- MISSION TIME  (T+MM:SS.S) ---
        secs = packet.mission_elapsed_seconds
        minutes = int(secs) // 60
        remaining = secs - minutes * 60
        self._time_value.setText(f"T+{minutes:02d}:{remaining:05.1f}")

        # --- LINK QUALITY ---
        link_quality = metrics.get("link_quality", 0.0)
        link_color = self._link_quality_color(link_quality)
        link_label = self._link_quality_label(link_quality)
        self._link_value.setText(f"{link_quality * 100:.0f}% {link_label}")
        self._link_value.setStyleSheet(
            f"color: {link_color.name()}; border: none;"
        )

        # --- PACKET RATE ---
        freq = metrics.get("frequency_hz", 0.0)
        self._rate_value.setText(f"{freq:.2f} Hz")

        # --- CORRUPTED ---
        failures = metrics.get("checksum_failures", 0)
        self._corrupt_value.setText(str(int(failures)))

        # --- HEARTBEAT ---
        self._pulse_heartbeat()

    # ------------------------------------------------------------------
    # Heartbeat
    # ------------------------------------------------------------------

    def _pulse_heartbeat(self) -> None:
        """Flash the heartbeat dot green and restart the timeout."""
        self._heartbeat_dot.setStyleSheet(
            f"color: {STATUS_NOMINAL.name()}; border: none;"
        )
        self._heartbeat_active = True
        self._heartbeat_timer.start()

    def _on_heartbeat_timeout(self) -> None:
        """No update received within the timeout — show stale indicator."""
        self._heartbeat_dot.setStyleSheet(
            f"color: {STATUS_INACTIVE.name()}; border: none;"
        )
        self._heartbeat_active = False

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _make_label(text: str) -> QLabel:
        """Create a grey label for the left column."""
        lbl = QLabel(text)
        lbl.setFont(font_status_label())
        lbl.setStyleSheet(
            f"color: {TEXT_SECONDARY.name()}; border: none;"
        )
        lbl.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        return lbl

    @staticmethod
    def _make_value() -> QLabel:
        """Create a white value label for the right column."""
        lbl = QLabel(_DEFAULT_VALUE)
        lbl.setFont(font_status_value())
        lbl.setStyleSheet(
            f"color: {TEXT_PRIMARY.name()}; border: none;"
        )
        lbl.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        return lbl

    @staticmethod
    def _resolve_state_color(packet) -> "QColor":
        """Pick the indicator color based on state name and fault flag."""
        if packet.fault_detected:
            return STATUS_CRITICAL
        return _STATE_COLORS.get(packet.state_name, STATUS_INACTIVE)

    @staticmethod
    def _link_quality_color(quality: float) -> "QColor":
        """Return a color for the link quality percentage."""
        if quality >= 0.7:
            return LINK_GOOD
        if quality >= 0.4:
            return LINK_POOR
        return LINK_CRITICAL

    @staticmethod
    def _link_quality_label(quality: float) -> str:
        """Return a human-readable label for link quality."""
        if quality >= 0.7:
            return "GOOD"
        if quality >= 0.4:
            return "FAIR"
        return "POOR"
