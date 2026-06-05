"""Command panel widget — uplink command buttons for the ground station.

Provides a grid of buttons for common uplink commands, organized into
three visual tiers: primary, secondary, and danger.  Each button click
emits the ``command_requested`` signal with the corresponding command
string.  The widget does **not** import or depend on any commanding
module — the MainWindow wires the signal to the appropriate dispatcher.
"""

from __future__ import annotations

from PyQt6.QtCore import pyqtSignal, Qt
from PyQt6.QtWidgets import (
    QFrame,
    QGridLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
)

from ui.theme import (
    BG_PANEL,
    BG_CARD,
    BORDER,
    BORDER_ACCENT,
    TEXT_PRIMARY,
    TEXT_SECONDARY,
    STATUS_CRITICAL,
    STATUS_WARNING,
    ACCENT,
    font_section_title,
    font_button,
)


# ---------------------------------------------------------------------------
# Command definitions: (label, command string, tier)
# Tiers: "primary" | "secondary" | "danger"
# ---------------------------------------------------------------------------

_COMMANDS: list[tuple[str, str, str]] = [
    # --- Primary ---
    ("ARM FLIGHT",       "CMD:ARM_FLIGHT",       "primary"),
    ("START TELEMETRY",  "CMD:START_TELEMETRY",   "primary"),
    # --- Secondary ---
    ("PING",             "CMD:PING",              "secondary"),
    ("REQUEST STATUS",   "CMD:REQUEST_STATUS",    "secondary"),
    ("ZERO SENSORS",     "CMD:ZERO_SENSORS",      "secondary"),
    # --- Danger ---
    ("DISARM FLIGHT",    "CMD:DISARM_FLIGHT",     "danger"),
    ("STOP TELEMETRY",   "CMD:STOP_TELEMETRY",    "danger"),
]

_GRID_COLUMNS = 2


class CommandPanelWidget(QFrame):
    """Panel with uplink command buttons in a tiered visual hierarchy."""

    # Emitted when any command button is clicked.  Payload is the raw
    # command string, e.g. "CMD:ARM_FLIGHT".
    command_requested = pyqtSignal(str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._build_ui()

    # --------------------------------------------------------------------- #
    # UI construction
    # --------------------------------------------------------------------- #

    def _build_ui(self) -> None:
        self.setFixedWidth(280)
        self.setStyleSheet(
            f"CommandPanelWidget {{"
            f"  background-color: {BG_PANEL.name()};"
            f"  border: 1px solid {BORDER.name()};"
            f"  border-radius: 6px;"
            f"}}"
        )

        root_layout = QVBoxLayout(self)
        root_layout.setContentsMargins(12, 10, 12, 12)
        root_layout.setSpacing(6)

        # --- Title ---
        title = QLabel("COMMANDS")
        title.setFont(font_section_title())
        title.setStyleSheet(f"color: {TEXT_PRIMARY.name()}; border: none;")
        title.setAlignment(Qt.AlignmentFlag.AlignLeft)
        root_layout.addWidget(title)

        # Group buttons by tier
        current_tier = None
        grid = None
        grid_idx = 0

        for label, cmd, tier in _COMMANDS:
            if tier != current_tier:
                # Start a new grid for this tier group
                if grid is not None:
                    root_layout.addLayout(grid)
                    # Small spacer between groups
                    root_layout.addSpacing(4)
                grid = QGridLayout()
                grid.setSpacing(6)
                grid_idx = 0
                current_tier = tier

            btn = self._make_button(label, cmd, tier)
            row = grid_idx // _GRID_COLUMNS
            col = grid_idx % _GRID_COLUMNS
            grid.addWidget(btn, row, col)
            grid_idx += 1

        # Add the last grid
        if grid is not None:
            root_layout.addLayout(grid)

        root_layout.addStretch()

    # --------------------------------------------------------------------- #
    # Button factory
    # --------------------------------------------------------------------- #

    def _make_button(self, label: str, command: str, tier: str) -> QPushButton:
        """Create a styled command button appropriate for its tier."""
        btn = QPushButton(label)
        btn.setFont(font_button())
        btn.setCursor(Qt.CursorShape.PointingHandCursor)

        if tier == "primary":
            # Slightly brighter background + accent left border
            btn.setStyleSheet(
                f"QPushButton {{"
                f"  background-color: #3a3a44;"
                f"  color: {TEXT_PRIMARY.name()};"
                f"  border: 1px solid {BORDER.name()};"
                f"  border-left: 3px solid {ACCENT.name()};"
                f"  border-radius: 4px;"
                f"  padding: 7px 4px;"
                f"}}"
                f"QPushButton:hover {{"
                f"  background-color: {BORDER_ACCENT.name()};"
                f"}}"
                f"QPushButton:pressed {{"
                f"  background-color: {ACCENT.name()};"
                f"}}"
            )
        elif tier == "danger":
            # Muted text + subtle red left border
            danger_border = STATUS_CRITICAL.name()
            btn.setStyleSheet(
                f"QPushButton {{"
                f"  background-color: {BG_CARD.name()};"
                f"  color: {TEXT_SECONDARY.name()};"
                f"  border: 1px solid {BORDER.name()};"
                f"  border-left: 3px solid {danger_border};"
                f"  border-radius: 4px;"
                f"  padding: 7px 4px;"
                f"}}"
                f"QPushButton:hover {{"
                f"  background-color: {BORDER_ACCENT.name()};"
                f"  color: {TEXT_PRIMARY.name()};"
                f"}}"
                f"QPushButton:pressed {{"
                f"  background-color: {STATUS_CRITICAL.name()};"
                f"  color: {TEXT_PRIMARY.name()};"
                f"}}"
            )
        else:
            # Secondary — default neutral styling
            btn.setStyleSheet(
                f"QPushButton {{"
                f"  background-color: {BG_CARD.name()};"
                f"  color: {TEXT_PRIMARY.name()};"
                f"  border: 1px solid {BORDER.name()};"
                f"  border-radius: 4px;"
                f"  padding: 6px 4px;"
                f"}}"
                f"QPushButton:hover {{"
                f"  background-color: {BORDER_ACCENT.name()};"
                f"}}"
                f"QPushButton:pressed {{"
                f"  background-color: {ACCENT.name()};"
                f"}}"
            )

        # Connect click → emit signal with the command string.
        btn.clicked.connect(lambda _checked, cmd=command: self.command_requested.emit(cmd))
        return btn
