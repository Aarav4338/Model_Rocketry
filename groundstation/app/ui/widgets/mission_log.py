"""Mission Log widget — scrolling log panel for the ground station.

Displays timestamped, color-coded log entries in a read-only text area.
Supports INFO, WARN, ERROR, and EVENT severity levels.
"""

from datetime import datetime

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QFrame, QLabel, QTextEdit, QVBoxLayout

from ui.theme import (
    BG_INPUT,
    BG_PANEL,
    BORDER,
    STATUS_CRITICAL,
    STATUS_NOMINAL,
    STATUS_WARNING,
    TEXT_PRIMARY,
    TEXT_SECONDARY,
    font_log,
    font_section_title,
)

# Maximum number of log entries before oldest are pruned.
_MAX_ENTRIES = 500


class MissionLogWidget(QFrame):
    """Bottom-panel widget showing a scrolling mission log."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)

        self._entry_count: int = 0

        self._build_ui()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        # Frame styling — rounded corners, dark background, subtle border.
        self.setStyleSheet(
            f"MissionLogWidget {{"
            f"  background-color: {BG_PANEL.name()};"
            f"  border: 1px solid {BORDER.name()};"
            f"  border-radius: 6px;"
            f"}}"
        )
        self.setMinimumHeight(150)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(4)

        # --- Title ---
        title = QLabel("MISSION LOG")
        title.setFont(font_section_title())
        title.setStyleSheet(f"color: {TEXT_PRIMARY.name()}; border: none; background: transparent;")
        layout.addWidget(title)

        # --- Log text area ---
        self._log_area = QTextEdit()
        self._log_area.setReadOnly(True)
        self._log_area.setFont(font_log())
        self._log_area.setStyleSheet(
            f"QTextEdit {{"
            f"  background-color: {BG_INPUT.name()};"
            f"  color: {TEXT_PRIMARY.name()};"
            f"  border: 1px solid {BORDER.name()};"
            f"  border-radius: 4px;"
            f"  padding: 4px;"
            f"}}"
        )
        layout.addWidget(self._log_area)

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def append_entry(self, message: str, level: str = "INFO") -> None:
        """Append a timestamped, color-coded log entry.

        Parameters
        ----------
        message:
            The log message text.
        level:
            Severity level — one of ``INFO``, ``WARN``, ``ERROR``, ``EVENT``.
        """
        level = level.upper()
        level_color = self._level_color(level)

        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:12]  # HH:MM:SS.mmm

        html_line = (
            f'<span style="color:{TEXT_SECONDARY.name()};">{timestamp}</span> '
            f'<span style="color:{level_color};font-weight:bold;">[{level}]</span> '
            f'<span style="color:{TEXT_PRIMARY.name()};">{message}</span>'
        )

        self._log_area.append(html_line)
        self._entry_count += 1

        # Prune oldest entries when the cap is exceeded.
        if self._entry_count > _MAX_ENTRIES:
            self._remove_oldest_entry()

        # Auto-scroll to the bottom.
        scrollbar = self._log_area.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def clear_log(self) -> None:
        """Remove all log entries."""
        self._log_area.clear()
        self._entry_count = 0

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _level_color(level: str) -> str:
        """Return the hex color string for a given severity level."""
        colors = {
            "INFO": TEXT_PRIMARY.name(),
            "WARN": STATUS_WARNING.name(),
            "ERROR": STATUS_CRITICAL.name(),
            "EVENT": STATUS_NOMINAL.name(),
        }
        return colors.get(level, TEXT_PRIMARY.name())

    def _remove_oldest_entry(self) -> None:
        """Remove the first block (oldest entry) from the document."""
        doc = self._log_area.document()
        cursor = self._log_area.textCursor()
        block = doc.begin()
        cursor.setPosition(block.position())
        cursor.movePosition(cursor.MoveOperation.EndOfBlock, cursor.MoveMode.KeepAnchor)
        # Also select the trailing newline so the line disappears cleanly.
        cursor.movePosition(cursor.MoveOperation.NextCharacter, cursor.MoveMode.KeepAnchor)
        cursor.removeSelectedText()
        self._entry_count -= 1
