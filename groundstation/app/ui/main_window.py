"""Main window for the Ground Station UI.

Assembles the three-column layout with bottom panel:
  LEFT:   MissionStatusWidget
  CENTER: AltitudeGraph (real-time)
  RIGHT:  8x TelemetryCard
  BOTTOM: MissionLogWidget + CommandPanelWidget

Connects TelemetryWorker signals to all widget update slots.
"""

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QLabel,
    QMainWindow,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from commanding.command_dispatcher import CommandDispatcher
from core.telemetry_manager import TelemetryManager
from transport.transport import Transport
from ui.telemetry_worker import TelemetryWorker
from ui.theme import (
    TEXT_PRIMARY,
    font_section_title,
)
from ui.widgets.altitude_graph import AltitudeGraph
from ui.widgets.command_panel import CommandPanelWidget
from ui.widgets.mission_log import MissionLogWidget
from ui.widgets.mission_status import MissionStatusWidget
from ui.widgets.telemetry_card import TelemetryCard


class MainWindow(QMainWindow):
    """Ground Station mission control window."""

    def __init__(
        self,
        manager: TelemetryManager,
        transport: Transport,
        poll_interval: float = 0.25,
        parent=None,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle("Ground Station — Mission Control")
        self.setMinimumSize(1280, 720)

        # --- Build backend bridge ---
        self.worker = TelemetryWorker(manager, poll_interval)
        self.command_dispatcher = CommandDispatcher(transport)

        # --- Build UI ---
        central = QWidget()
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)
        root_layout.setContentsMargins(8, 8, 8, 8)
        root_layout.setSpacing(8)

        # Top section: three columns
        top_splitter = QSplitter(Qt.Orientation.Horizontal)
        top_splitter.setHandleWidth(4)
        top_splitter.setStyleSheet("""
            QSplitter::handle {
                background-color: #2a2a30;
            }
        """)

        # --- LEFT: Mission Status ---
        self.mission_status = MissionStatusWidget()
        top_splitter.addWidget(self.mission_status)

        # --- CENTER: Altitude Graph ---
        self.altitude_graph = AltitudeGraph(buffer_size=500)
        top_splitter.addWidget(self.altitude_graph)

        # --- RIGHT: Telemetry Cards ---
        right_widget = self._build_telemetry_cards()
        top_splitter.addWidget(right_widget)

        # Set splitter proportions: left=2, center=5, right=3
        top_splitter.setSizes([280, 550, 350])
        top_splitter.setStretchFactor(0, 2)
        top_splitter.setStretchFactor(1, 5)
        top_splitter.setStretchFactor(2, 3)

        root_layout.addWidget(top_splitter, stretch=7)

        # --- BOTTOM: Log + Commands ---
        bottom_splitter = QSplitter(Qt.Orientation.Horizontal)
        bottom_splitter.setHandleWidth(4)
        bottom_splitter.setStyleSheet("""
            QSplitter::handle {
                background-color: #2a2a30;
            }
        """)

        self.mission_log = MissionLogWidget()
        bottom_splitter.addWidget(self.mission_log)

        self.command_panel = CommandPanelWidget()
        bottom_splitter.addWidget(self.command_panel)

        bottom_splitter.setSizes([700, 280])
        bottom_splitter.setStretchFactor(0, 7)
        bottom_splitter.setStretchFactor(1, 3)

        root_layout.addWidget(bottom_splitter, stretch=3)

        # Track latest values for the mission status widget
        self._last_packet = None
        self._last_metrics = {}

        # --- Wire signals ---
        self._connect_signals()

    def _build_telemetry_cards(self) -> QWidget:
        """Build the right panel with 8 telemetry cards in a 2-column grid."""
        container = QWidget()
        layout = QVBoxLayout(container)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        title = QLabel("TELEMETRY")
        title.setFont(font_section_title())
        title.setStyleSheet(f"color: {TEXT_PRIMARY.name()}; padding: 4px 8px;")
        layout.addWidget(title)

        # Grid of cards
        from PyQt6.QtWidgets import QGridLayout
        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(16)
        grid.setContentsMargins(8, 8, 8, 8)

        self.telemetry_cards = []

        card_defs = [
            ("ALTITUDE",     "m",    "altitude",              ".2f"),
            ("VELOCITY",     "m/s",  "vertical_velocity",     ".2f"),
            ("IMU ACCEL",    "m/s²", "accelerometer_magnitude", ".2f"),
            ("PRESSURE",     "Pa",   "pressure",              ".1f"),
            ("TEMPERATURE",  "°C",   "temperature",           ".1f"),
            ("VOLTAGE",      "V",    "voltage",               ".2f"),
            ("GPS",          "°",    "gps_coordinates",       ".5f"),
            ("SATELLITES",   "",     "gnss_sats",             "d"),
        ]

        for i, (label, unit, field, fmt) in enumerate(card_defs):
            card = TelemetryCard(label, unit, field, fmt)
            self.telemetry_cards.append(card)
            row = i // 2
            col = i % 2
            grid.addWidget(card, row, col)

        layout.addLayout(grid, stretch=1)
        return container

    def _connect_signals(self) -> None:
        """Wire worker signals to widget update slots."""
        # Valid packet → update telemetry cards + mission status + graph
        self.worker.packet_received.connect(self._on_packet_received)

        # Stats → update mission status link/rate/corruption
        self.worker.stats_updated.connect(self._on_stats_updated)

        # Log messages → mission log
        self.worker.log_message.connect(self._on_log_message)

        # Command panel → command dispatcher
        self.command_panel.command_requested.connect(self._on_command_requested)

    def _on_packet_received(self, packet) -> None:
        """Handle a valid telemetry packet."""
        self._last_packet = packet

        # Update all telemetry cards
        for card in self.telemetry_cards:
            card.update_value(packet)

        # Update mission status with latest packet + cached metrics
        self.mission_status.update_status(packet, self._last_metrics)

        # Update altitude graph
        self.altitude_graph.append_point(
            packet.mission_elapsed_seconds,
            packet.altitude,
        )

    def _on_stats_updated(self, metrics: dict) -> None:
        """Handle updated backend metrics."""
        self._last_metrics = metrics

        # If we have a packet, refresh mission status with new metrics
        if self._last_packet is not None:
            self.mission_status.update_status(self._last_packet, metrics)

    def _on_log_message(self, message: str, level: str) -> None:
        """Handle a log message from the worker."""
        self.mission_log.append_entry(message, level)

    def _on_command_requested(self, command: str) -> None:
        """Handle a command request from the command panel."""
        try:
            self.command_dispatcher.send(command)
            self.mission_log.append_entry(f"Command sent: {command}", "INFO")
        except Exception as exc:
            self.mission_log.append_entry(f"Command failed: {exc}", "ERROR")

    def start(self) -> None:
        """Start the telemetry worker thread."""
        self.worker.start()

    def closeEvent(self, event) -> None:
        """Cleanly stop the worker when the window is closed."""
        self.mission_log.append_entry("Shutting down...", "EVENT")
        self.worker.request_stop()
        self.worker.wait(5000)  # Wait up to 5s for clean shutdown
        event.accept()
