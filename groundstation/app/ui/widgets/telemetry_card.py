"""Reusable card widget that displays a single telemetry value.

Instantiate once per metric (altitude, velocity, etc.) and call
``update_value(packet)`` whenever a new TelemetryPacket arrives.
"""

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QLabel, QVBoxLayout, QWidget

from ui.theme import (
    BG_CARD,
    BORDER,
    TEXT_PRIMARY,
    TEXT_SECONDARY,
    font_gps_value,
    font_label,
    font_unit,
    font_value,
)

_NO_DATA = "---"


class TelemetryCard(QWidget):
    """A compact card showing *label → value → unit* for one telemetry field."""

    def __init__(
        self,
        label: str,
        unit: str,
        field_name: str,
        format_str: str = ".2f",
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)

        self._field_name = field_name
        self._format_str = format_str

        # ---- styling -------------------------------------------------------
        bg = BG_CARD.name()
        border = BORDER.name()
        self.setStyleSheet(
            f"TelemetryCard {{"
            f"  background-color: {bg};"
            f"  border: 1px solid {border};"
            f"  border-radius: 8px;"
            f"}}"
        )
        self.setMinimumSize(180, 100)

        # ---- label (top) ---------------------------------------------------
        self._label = QLabel(label)
        self._label.setFont(font_label())
        self._label.setAlignment(Qt.AlignmentFlag.AlignHCenter)
        self._label.setStyleSheet(f"color: {TEXT_SECONDARY.name()}; border: none;")

        # ---- value (centre) ------------------------------------------------
        # GPS coordinates need a smaller font to fit the full pair
        value_font = font_gps_value() if field_name == "gps_coordinates" else font_value()
        self._value = QLabel(_NO_DATA)
        self._value.setFont(value_font)
        self._value.setAlignment(Qt.AlignmentFlag.AlignHCenter)
        self._value.setStyleSheet(f"color: {TEXT_PRIMARY.name()}; border: none;")

        # ---- unit (bottom) -------------------------------------------------
        self._unit = QLabel(unit)
        self._unit.setFont(font_unit())
        self._unit.setAlignment(Qt.AlignmentFlag.AlignHCenter)
        self._unit.setStyleSheet(f"color: {TEXT_SECONDARY.name()}; border: none;")

        # ---- layout --------------------------------------------------------
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 12, 8, 12)
        layout.setSpacing(4)
        layout.addWidget(self._label)
        layout.addStretch()
        layout.addWidget(self._value)
        layout.addStretch()
        layout.addWidget(self._unit)

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def update_value(self, packet) -> None:
        """Read the relevant field from *packet* and update the display."""
        if self._field_name == "gps_coordinates":
            lat = packet.gnss_latitude
            lon = packet.gnss_longitude
            if lat == 0.0 and lon == 0.0:
                text = "NO FIX"
            else:
                text = f"{lat:.5f}°\n{lon:.5f}°"
        elif self._field_name == "gnss_sats":
            text = str(int(getattr(packet, self._field_name)))
        else:
            raw = getattr(packet, self._field_name)
            text = f"{raw:{self._format_str}}"

        self._value.setText(text)
