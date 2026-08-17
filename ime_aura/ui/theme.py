"""Signal Edge design tokens and stylesheets."""

from __future__ import annotations

import os
import sys

from PySide6.QtGui import QColor, QFont, QFontDatabase


# Brand / Signal Edge
ACCENT_JP = QColor(248, 40, 70)
ACCENT_EN = QColor(45, 129, 253)

# Semantic (light instrument panel)
BG = QColor(248, 249, 252)
BG_WASH_JP = QColor(248, 40, 70, 28)
BG_WASH_EN = QColor(45, 129, 253, 28)
TEXT_PRIMARY = QColor(28, 28, 30)
TEXT_SECONDARY = QColor(90, 90, 98)
SEPARATOR = QColor(60, 60, 67, 45)
FILL = QColor(120, 120, 128, 28)
FILL_HOVER = QColor(120, 120, 128, 72)
FILL_PRESSED = QColor(120, 120, 128, 110)
SURFACE = QColor(255, 255, 255, 230)
DANGER = QColor(200, 50, 50)
DANGER_FILL = QColor(200, 50, 50, 22)
DANGER_FILL_HOVER = QColor(200, 50, 50, 38)
FOCUS_RING = QColor(45, 129, 253, 160)
CHECKER_LIGHT = QColor(255, 255, 255)
CHECKER_DARK = QColor(220, 220, 226)

# Spacing (8pt grid)
SPACE_1 = 4
SPACE_2 = 8
SPACE_3 = 12
SPACE_4 = 16
SPACE_5 = 24
SPACE_6 = 32

MARGIN = SPACE_5
GAP = SPACE_2
ROW_GAP = SPACE_2
SECTION_GAP = SPACE_4

# Compact settings column; wide enough for Japanese control labels.
MIN_WINDOW_WIDTH = 360
MIN_WINDOW_HEIGHT = 560

# Controls: padding stays fixed so height follows the font
BUTTON_PAD_Y = 8
BUTTON_PAD_X = 14
SWATCH_HEIGHT = 32
SWATCH_WIDTH = 104
# Capsule ends: corner radius is 50% of control height.
RADIUS_CONTROL = 12
RADIUS_SEGMENT = 10
HIT_MIN = 32

# Scrollbar
SCROLLBAR_WIDTH = 12
SCROLLBAR_MARGIN_Y = 8
SCROLLBAR_MARGIN_RIGHT = 3
SCROLLBAR_GUTTER = SCROLLBAR_WIDTH + SCROLLBAR_MARGIN_RIGHT + 3

# Motion
FADE_MS = 150
STATUS_BLEND_MS = 180
BUTTON_PRESS_MS = 90
HOVER_MS = 140
ENTRANCE_MS = 240
REVEAL_MS = 220
INDICATOR_MS = 180
STATUS_FLASH_MS = 1100

_REDUCED_MOTION: bool | None = None


def prefers_reduced_motion() -> bool:
    """Honor OS reduce-motion settings (cached for the process)."""
    global _REDUCED_MOTION
    if _REDUCED_MOTION is None:
        _REDUCED_MOTION = _detect_reduced_motion()
    return _REDUCED_MOTION


def motion_ms(duration: int) -> int:
    return 0 if prefers_reduced_motion() else max(0, int(duration))


def _detect_reduced_motion() -> bool:
    if os.environ.get("IMEAURA_REDUCED_MOTION") == "1":
        return True
    if sys.platform == "win32":
        try:
            import ctypes

            enabled = ctypes.c_int(1)
            # SPI_GETCLIENTAREAANIMATION: 0 means the user asked to minimize animation.
            if ctypes.windll.user32.SystemParametersInfoW(0x1042, 0, ctypes.byref(enabled), 0):
                return enabled.value == 0
        except (AttributeError, OSError, ValueError):
            return False
        return False
    if sys.platform == "darwin":
        try:
            from subprocess import run

            result = run(
                ["defaults", "read", "com.apple.universalaccess", "reduceMotion"],
                capture_output=True,
                text=True,
                timeout=1,
            )
            return result.stdout.strip() == "1"
        except (OSError, TimeoutError):
            return False
    gtk = os.environ.get("GTK_THEME", "")
    return "highcontrast" in gtk.lower() and "inverse" not in gtk.lower()


def _rgba(c: QColor) -> str:
    return f"rgba({c.red()}, {c.green()}, {c.blue()}, {c.alpha()})"


def _rgb(c: QColor) -> str:
    return f"rgb({c.red()}, {c.green()}, {c.blue()})"


def ui_font_families() -> list[str]:
    """Prefer platform UI fonts (HIG: system typography) with CJK coverage."""
    if sys.platform == "darwin":
        return [
            "Hiragino Sans",
            "Hiragino Kaku Gothic ProN",
            ".AppleSystemUIFont",
            "SF Pro Text",
        ]
    if sys.platform == "win32":
        return [
            "Yu Gothic UI",
            "Yu Gothic",
            "Meiryo UI",
            "Meiryo",
            "Segoe UI",
            "Segoe UI Variable",
        ]
    return [
        "Noto Sans CJK JP",
        "Noto Sans JP",
        "Source Han Sans JP",
        "DejaVu Sans",
        "Sans Serif",
    ]


def resolve_ui_font_family() -> str:
    available = set(QFontDatabase.families())
    for name in ui_font_families():
        if name in available:
            return name
        for fam in available:
            if fam.lower() == name.lower():
                return fam
    return QFont().defaultFamily()


def resolve_ui_font_families() -> list[str]:
    """Ordered family list so Latin UI fonts fall back to CJK-capable faces."""
    available = {f.lower(): f for f in QFontDatabase.families()}
    resolved: list[str] = []
    for name in ui_font_families():
        if name in QFontDatabase.families():
            resolved.append(name)
            continue
        match = available.get(name.lower())
        if match and match not in resolved:
            resolved.append(match)
    if not resolved:
        resolved.append(QFont().defaultFamily())
    return resolved


def apply_app_font(point_size: int) -> QFont:
    font = QFont()
    font.setFamilies(resolve_ui_font_families())
    font.setPointSize(point_size)
    font.setStyleHint(QFont.StyleHint.SansSerif)
    return font


def application_stylesheet() -> str:
    """Global QSS for control surfaces. Tokens only — no purple / dark bias."""
    return f"""
    QWidget#ControlRoot, QDialog#AboutDialog {{
        background: transparent;
        color: {_rgb(TEXT_PRIMARY)};
    }}

    QLabel {{
        color: {_rgb(TEXT_PRIMARY)};
        background: transparent;
    }}
    QLabel#BrandTitle {{
        font-weight: 600;
        color: {_rgb(TEXT_PRIMARY)};
    }}
    QLabel#BrandTagline, QLabel#SectionHint, QLabel#SecondaryLabel {{
        color: {_rgb(TEXT_SECONDARY)};
    }}
    QLabel#SectionTitle {{
        font-weight: 600;
        color: {_rgb(TEXT_PRIMARY)};
    }}

    QFrame#Hairline {{
        background-color: {_rgba(SEPARATOR)};
        border: none;
        max-height: 1px;
        min-height: 1px;
    }}

    QPushButton#PaintedButton {{
        background: transparent;
        border: none;
        padding: {BUTTON_PAD_Y}px {BUTTON_PAD_X}px;
        min-height: {HIT_MIN - BUTTON_PAD_Y * 2}px;
    }}
    QPushButton#PaintedButton:focus {{
        outline: none;
        border: none;
    }}

    QPushButton {{
        background-color: {_rgba(FILL)};
        color: {_rgb(TEXT_PRIMARY)};
        border: 1px solid {_rgba(SEPARATOR)};
        border-radius: {RADIUS_CONTROL}px;
        padding: {BUTTON_PAD_Y}px {BUTTON_PAD_X}px;
    }}
    QPushButton:hover {{
        background-color: {_rgba(FILL_HOVER)};
        border: 1px solid {_rgba(QColor(60, 60, 67, 90))};
    }}
    QPushButton:pressed {{
        background-color: {_rgba(FILL_PRESSED)};
    }}
    QPushButton:focus {{
        border: 2px solid {_rgba(FOCUS_RING)};
    }}
    QPushButton#DangerButton {{
        color: {_rgb(DANGER)};
        background-color: {_rgba(DANGER_FILL)};
        border: 1px solid {_rgba(QColor(200, 50, 50, 70))};
    }}
    QPushButton#DangerButton:hover {{
        background-color: {_rgba(DANGER_FILL_HOVER)};
        border: 1px solid {_rgba(QColor(200, 50, 50, 110))};
        color: {_rgb(DANGER)};
    }}
    QPushButton#DangerButton:pressed {{
        background-color: {_rgba(QColor(200, 50, 50, 55))};
    }}
    QPushButton#SecondaryButton {{
        background-color: {_rgba(FILL)};
        border: 1px solid {_rgba(SEPARATOR)};
    }}
    QPushButton#SecondaryButton:hover {{
        background-color: {_rgba(FILL_HOVER)};
        border: 1px solid {_rgba(QColor(45, 129, 253, 120))};
        color: {_rgb(TEXT_PRIMARY)};
    }}
    QPushButton#SecondaryButton:pressed {{
        background-color: {_rgba(FILL_PRESSED)};
        border: 1px solid {_rgba(QColor(45, 129, 253, 160))};
    }}
    QPushButton#QuietButton {{
        background-color: transparent;
        border: 1px solid transparent;
        color: {_rgb(TEXT_SECONDARY)};
        text-align: left;
        padding-left: {SPACE_2}px;
    }}
    QPushButton#QuietButton:hover {{
        background-color: {_rgba(FILL)};
        border: 1px solid {_rgba(SEPARATOR)};
        color: {_rgb(TEXT_PRIMARY)};
    }}
    QPushButton#QuietButton:pressed {{
        background-color: {_rgba(FILL_PRESSED)};
        color: {_rgb(TEXT_PRIMARY)};
    }}
    QPushButton#QuietButton:focus {{
        border: 2px solid {_rgba(FOCUS_RING)};
        color: {_rgb(TEXT_PRIMARY)};
    }}

    QWidget#SegmentedTrack {{
        background-color: {_rgba(FILL)};
        border: 1px solid {_rgba(SEPARATOR)};
        border-radius: {RADIUS_CONTROL}px;
    }}
    QWidget#SegmentedIndicator {{
        background-color: {_rgba(SURFACE)};
        border: 1px solid {_rgba(QColor(60, 60, 67, 40))};
        border-radius: {RADIUS_SEGMENT}px;
    }}
    QPushButton#SegmentedButton {{
        background: transparent;
        border: none;
        color: {_rgb(TEXT_SECONDARY)};
        padding: {BUTTON_PAD_Y}px {SPACE_3}px;
        min-height: {HIT_MIN - BUTTON_PAD_Y * 2}px;
        border-radius: {RADIUS_SEGMENT}px;
    }}
    QPushButton#SegmentedButton:hover {{
        background: transparent;
        border: none;
        color: {_rgb(TEXT_PRIMARY)};
    }}
    QPushButton#SegmentedButton:checked {{
        background: transparent;
        border: none;
        color: {_rgb(TEXT_PRIMARY)};
        font-weight: 600;
    }}
    QPushButton#SegmentedButton:focus {{
        border: none;
        outline: none;
    }}
    QPushButton#SegmentedButton:pressed {{
        background: transparent;
        border: none;
    }}

    QRadioButton, QCheckBox {{
        color: {_rgb(TEXT_PRIMARY)};
        spacing: {GAP}px;
        padding: {SPACE_1}px 0;
        min-height: {HIT_MIN}px;
    }}
    QRadioButton::indicator, QCheckBox::indicator {{
        width: 18px;
        height: 18px;
    }}
    QRadioButton:disabled, QCheckBox:disabled {{
        color: {_rgba(QColor(60, 60, 67, 110))};
    }}
    QRadioButton:focus, QCheckBox:focus {{
        outline: none;
    }}

    QSlider::groove:horizontal {{
        height: 6px;
        background: {_rgba(SEPARATOR)};
        border-radius: 3px;
        margin: 0 2px;
    }}
    QSlider::handle:horizontal {{
        width: 20px;
        height: 20px;
        margin: -7px 0;
        background: {_rgb(ACCENT_EN)};
        border: 2px solid rgb(255, 255, 255);
        border-radius: 10px;
    }}
    QSlider::handle:horizontal:hover {{
        background: {_rgb(QColor(32, 114, 245))};
    }}
    QSlider::handle:horizontal:pressed {{
        background: {_rgb(QColor(24, 98, 220))};
    }}
    QSlider::sub-page:horizontal {{
        background: {_rgba(QColor(45, 129, 253, 160))};
        border-radius: 3px;
        margin: 0 2px;
    }}
    QSlider:horizontal {{
        padding: {BUTTON_PAD_Y}px {GAP}px;
        margin: 0;
        min-height: {HIT_MIN}px;
    }}

    QSpinBox {{
        background-color: {_rgba(FILL)};
        border: 1px solid {_rgba(SEPARATOR)};
        border-radius: {RADIUS_CONTROL}px;
        padding: {BUTTON_PAD_Y}px {GAP}px;
        color: {_rgb(TEXT_PRIMARY)};
        min-height: {HIT_MIN - BUTTON_PAD_Y * 2}px;
    }}
    QSpinBox:hover {{
        background-color: {_rgba(FILL_HOVER)};
        border: 1px solid {_rgba(QColor(60, 60, 67, 90))};
    }}
    QSpinBox:focus {{
        border: 2px solid {_rgba(FOCUS_RING)};
    }}

    QScrollArea {{
        background: transparent;
        border: none;
    }}
    QScrollArea#ControlScroll {{
        background: transparent;
        border: none;
    }}
    QScrollBar:vertical {{
        background: transparent;
        width: {SCROLLBAR_WIDTH}px;
        margin: {SCROLLBAR_MARGIN_Y}px {SCROLLBAR_MARGIN_RIGHT}px {SCROLLBAR_MARGIN_Y}px 0;
        border: none;
    }}
    QScrollBar::handle:vertical {{
        background: {_rgba(QColor(60, 60, 67, 70))};
        border-radius: {SCROLLBAR_WIDTH // 2}px;
        min-height: 32px;
    }}
    QScrollBar::handle:vertical:hover {{
        background: {_rgba(QColor(60, 60, 67, 120))};
    }}
    QScrollBar::handle:vertical:pressed {{
        background: {_rgba(QColor(60, 60, 67, 160))};
    }}
    QScrollBar::add-line:vertical,
    QScrollBar::sub-line:vertical,
    QScrollBar::up-arrow:vertical,
    QScrollBar::down-arrow:vertical {{
        height: 0;
        width: 0;
        background: none;
        border: none;
        image: none;
    }}
    QScrollBar::add-page:vertical,
    QScrollBar::sub-page:vertical {{
        background: transparent;
    }}
    QScrollBar:horizontal {{
        height: 0px;
        max-height: 0px;
        background: transparent;
        border: none;
    }}

    QTextBrowser {{
        background-color: {_rgba(FILL)};
        border: 1px solid {_rgba(SEPARATOR)};
        border-radius: {RADIUS_CONTROL}px;
        padding: 10px;
        color: {_rgb(TEXT_PRIMARY)};
    }}

    QDialogButtonBox QPushButton {{
        min-width: 80px;
    }}
    """


def color_dialog_stylesheet() -> str:
    """Tighten QColorDialog controls: smaller arrows, no square chrome."""
    return f"""
    QColorDialog {{
        background-color: {_rgb(BG)};
        color: {_rgb(TEXT_PRIMARY)};
    }}
    QColorDialog QLabel {{
        color: {_rgb(TEXT_PRIMARY)};
        background: transparent;
        padding: 0;
        margin: 0;
    }}
    QColorDialog QSpinBox,
    QColorDialog QDoubleSpinBox {{
        background-color: {_rgba(FILL)};
        border: 1px solid {_rgba(SEPARATOR)};
        border-radius: 8px;
        padding: 2px 4px;
        min-height: 24px;
        max-height: 28px;
        color: {_rgb(TEXT_PRIMARY)};
    }}
    QColorDialog QSpinBox::up-button,
    QColorDialog QSpinBox::down-button,
    QColorDialog QDoubleSpinBox::up-button,
    QColorDialog QDoubleSpinBox::down-button {{
        subcontrol-origin: border;
        width: 12px;
        border: none;
        background: transparent;
    }}
    QColorDialog QSpinBox::up-arrow,
    QColorDialog QDoubleSpinBox::up-arrow {{
        width: 6px;
        height: 6px;
        background: transparent;
    }}
    QColorDialog QSpinBox::down-arrow,
    QColorDialog QDoubleSpinBox::down-arrow {{
        width: 6px;
        height: 6px;
        background: transparent;
    }}
    QColorDialog QPushButton {{
        background-color: {_rgba(FILL)};
        border: 1px solid {_rgba(SEPARATOR)};
        border-radius: 10px;
        padding: 6px 12px;
        min-height: 26px;
    }}
    QColorDialog QPushButton:hover {{
        background-color: {_rgba(FILL_HOVER)};
    }}
    QColorDialog QPushButton:pressed {{
        background-color: {_rgba(FILL_PRESSED)};
    }}
    QColorDialog QWellArray,
    QColorDialog QColorShower,
    QColorDialog QColorLuminancePicker {{
        border: none;
        background: transparent;
    }}
    """
