"""Dark theme palette and style constants for the Ground Station UI.

Designed for readability at distance in competition environments.
Matte dark backgrounds, high-contrast text, color-coded status indicators.
"""

from PyQt6.QtGui import QColor, QFont, QPalette
from PyQt6.QtWidgets import QApplication


# ---------------------------------------------------------------------------
# Color palette
# ---------------------------------------------------------------------------

BG_PRIMARY = QColor(30, 30, 34)        # Main window background
BG_PANEL = QColor(40, 40, 46)          # Panel / card background
BG_CARD = QColor(50, 50, 58)           # Individual card background
BG_INPUT = QColor(38, 38, 44)          # Input fields, log area

BORDER = QColor(65, 65, 75)            # Subtle borders
BORDER_ACCENT = QColor(90, 90, 110)    # Focused / highlighted borders

TEXT_PRIMARY = QColor(230, 230, 235)   # Main text
TEXT_SECONDARY = QColor(160, 160, 170) # Labels, units, captions
TEXT_DIM = QColor(110, 110, 120)       # Placeholder text

# Status colors
STATUS_NOMINAL = QColor(76, 195, 120)  # Green — all good
STATUS_WARNING = QColor(240, 180, 50)  # Amber — attention
STATUS_CRITICAL = QColor(230, 70, 70)  # Red — fault / critical
STATUS_INACTIVE = QColor(100, 100, 110)  # Grey — no data

# Link quality gradient
LINK_EXCELLENT = QColor(76, 195, 120)
LINK_GOOD = QColor(140, 200, 80)
LINK_POOR = QColor(240, 180, 50)
LINK_CRITICAL = QColor(230, 70, 70)

# Accent
ACCENT = QColor(80, 140, 240)          # Buttons, highlights


# ---------------------------------------------------------------------------
# Fonts
# ---------------------------------------------------------------------------

def font_value() -> QFont:
    """Large font for telemetry values — readable from distance."""
    f = QFont("Consolas", 22)
    f.setBold(True)
    return f


def font_label() -> QFont:
    """Small font for card labels."""
    f = QFont("Segoe UI", 10)
    return f


def font_unit() -> QFont:
    """Small font for units."""
    f = QFont("Consolas", 10)
    return f


def font_status_value() -> QFont:
    """Medium-large font for status panel values."""
    f = QFont("Consolas", 16)
    f.setBold(True)
    return f


def font_status_label() -> QFont:
    """Font for status panel labels."""
    f = QFont("Segoe UI", 10)
    return f


def font_log() -> QFont:
    """Monospace font for the mission log."""
    f = QFont("Consolas", 9)
    return f


def font_button() -> QFont:
    """Font for command buttons."""
    f = QFont("Segoe UI", 10)
    f.setBold(True)
    return f


def font_section_title() -> QFont:
    """Font for section headers."""
    f = QFont("Segoe UI", 11)
    f.setBold(True)
    return f


def font_state_hero() -> QFont:
    """Large font for the hero state display — visible from distance."""
    f = QFont("Consolas", 28)
    f.setBold(True)
    return f


def font_gps_value() -> QFont:
    """Slightly smaller value font for GPS coordinates."""
    f = QFont("Consolas", 16)
    f.setBold(True)
    return f


# ---------------------------------------------------------------------------
# Apply dark palette to the entire application
# ---------------------------------------------------------------------------

def apply_dark_theme(app: QApplication) -> None:
    """Apply a matte dark theme to the QApplication."""
    palette = QPalette()

    palette.setColor(QPalette.ColorRole.Window, BG_PRIMARY)
    palette.setColor(QPalette.ColorRole.WindowText, TEXT_PRIMARY)
    palette.setColor(QPalette.ColorRole.Base, BG_INPUT)
    palette.setColor(QPalette.ColorRole.AlternateBase, BG_PANEL)
    palette.setColor(QPalette.ColorRole.ToolTipBase, BG_PANEL)
    palette.setColor(QPalette.ColorRole.ToolTipText, TEXT_PRIMARY)
    palette.setColor(QPalette.ColorRole.Text, TEXT_PRIMARY)
    palette.setColor(QPalette.ColorRole.Button, BG_CARD)
    palette.setColor(QPalette.ColorRole.ButtonText, TEXT_PRIMARY)
    palette.setColor(QPalette.ColorRole.BrightText, QColor(255, 255, 255))
    palette.setColor(QPalette.ColorRole.Link, ACCENT)
    palette.setColor(QPalette.ColorRole.Highlight, ACCENT)
    palette.setColor(QPalette.ColorRole.HighlightedText, QColor(255, 255, 255))
    palette.setColor(QPalette.ColorRole.PlaceholderText, TEXT_DIM)

    # Disabled state
    palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.WindowText, TEXT_DIM)
    palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Text, TEXT_DIM)
    palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.ButtonText, TEXT_DIM)

    app.setPalette(palette)

    # Global stylesheet additions
    app.setStyleSheet("""
        QToolTip {
            background-color: #32323a;
            color: #e6e6eb;
            border: 1px solid #5a5a6e;
            padding: 4px;
        }
        QScrollBar:vertical {
            background: #1e1e22;
            width: 10px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #50505a;
            min-height: 30px;
            border-radius: 5px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
    """)
