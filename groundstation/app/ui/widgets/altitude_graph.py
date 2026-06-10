"""Real-time altitude graph widget using pyqtgraph.

Plots altitude vs mission_elapsed_seconds with a fixed-size numpy
rolling buffer.  Only calls PlotDataItem.setData() once per frame —
pyqtgraph handles the diff internally.
"""

import numpy as np
import pyqtgraph as pg
from PyQt6.QtWidgets import QVBoxLayout, QWidget, QLabel
from PyQt6.QtCore import Qt

from ui.theme import (
    BG_PANEL,
    BORDER,
    TEXT_DIM,
    TEXT_PRIMARY,
    font_section_title,
)

# Desaturated blue — professional, dark-theme compatible
_CURVE_COLOR = (120, 180, 240)
_CURVE_WIDTH = 2

# Time window for x-axis auto-scroll (seconds)
_DEFAULT_WINDOW = 40.0


class _RollingBuffer:
    """Fixed-size ring buffer backed by pre-allocated numpy arrays.

    No list appends, no reallocation, no GC pressure.
    """

    __slots__ = ("_time", "_alt", "_count", "_capacity")

    def __init__(self, capacity: int = 500) -> None:
        self._time = np.full(capacity, np.nan, dtype=np.float64)
        self._alt = np.full(capacity, np.nan, dtype=np.float64)
        self._count: int = 0
        self._capacity: int = capacity

    def append(self, t: float, alt: float) -> None:
        idx = self._count % self._capacity
        self._time[idx] = t
        self._alt[idx] = alt
        self._count += 1

    def get_data(self) -> tuple[np.ndarray, np.ndarray]:
        """Return (time, altitude) arrays in chronological order."""
        if self._count == 0:
            return np.array([]), np.array([])
        if self._count <= self._capacity:
            return self._time[: self._count].copy(), self._alt[: self._count].copy()
        # Ring has wrapped — re-order so oldest comes first
        start = self._count % self._capacity
        t = np.concatenate([self._time[start:], self._time[:start]])
        a = np.concatenate([self._alt[start:], self._alt[:start]])
        return t, a

    @property
    def count(self) -> int:
        return self._count


class AltitudeGraph(QWidget):
    """Real-time altitude-vs-time graph with auto-scrolling x-axis."""

    def __init__(self, buffer_size: int = 500, parent=None) -> None:
        super().__init__(parent)

        self._buffer = _RollingBuffer(capacity=buffer_size)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        # Section title
        title = QLabel("ALTITUDE VS TIME")
        title.setFont(font_section_title())
        title.setStyleSheet(f"color: {TEXT_PRIMARY.name()}; padding: 4px 8px;")
        layout.addWidget(title)

        # pyqtgraph plot
        self._plot_widget = pg.PlotWidget()
        self._plot_widget.setBackground(BG_PANEL.name())
        self._plot_widget.showGrid(x=True, y=True, alpha=0.15)

        # Axis labels
        self._plot_widget.setLabel("bottom", "Mission Time", units="s",
                                   **{"color": TEXT_DIM.name()})
        self._plot_widget.setLabel("left", "Altitude", units="m",
                                   **{"color": TEXT_DIM.name()})

        # Axis styling
        for axis_name in ("bottom", "left"):
            axis = self._plot_widget.getAxis(axis_name)
            axis.setPen(BORDER.name())
            axis.setTextPen(TEXT_DIM.name())

        # Disable interactive pan/zoom — this is a live display
        self._plot_widget.setMouseEnabled(x=False, y=False)
        self._plot_widget.hideButtons()

        # Auto-range y-axis, manual x-axis (we scroll it ourselves)
        self._plot_widget.enableAutoRange(axis="y")
        self._plot_widget.setAutoVisible(y=True)

        # Create the curve item once — only setData() is called per frame
        self._curve = self._plot_widget.plot(
            pen=pg.mkPen(color=_CURVE_COLOR, width=_CURVE_WIDTH),
            antialias=True,
        )

        layout.addWidget(self._plot_widget, stretch=1)

        # Panel styling
        self.setStyleSheet(f"""
            AltitudeGraph {{
                background-color: {BG_PANEL.name()};
                border: 1px solid {BORDER.name()};
                border-radius: 6px;
            }}
        """)

    def append_point(self, t: float, altitude: float) -> None:
        """Add a data point and refresh the graph.

        Called once per valid packet from the main thread — fast path.
        """
        self._buffer.append(t, altitude)
        time_data, alt_data = self._buffer.get_data()

        # Update curve data (pyqtgraph diffs internally)
        self._curve.setData(time_data, alt_data)

        # Auto-scroll x-axis
        if len(time_data) > 0:
            t_max = time_data[-1]
            self._plot_widget.setXRange(t_max - _DEFAULT_WINDOW, t_max, padding=0.02)
