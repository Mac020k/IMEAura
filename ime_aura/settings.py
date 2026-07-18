"""Persistent application settings for IME Aura."""

from __future__ import annotations

import json
import logging
import os
import sys
from dataclasses import dataclass

from PySide6.QtGui import QColor

logger = logging.getLogger(__name__)

DEFAULT_COLOR_JP = QColor(248, 40, 70, 255)
DEFAULT_COLOR_EN = QColor(45, 129, 253, 255)

DISPLAY_MODE_ALWAYS = "always"
DISPLAY_MODE_ON_FOCUS = "on_focus"
DISPLAY_MODES = frozenset({DISPLAY_MODE_ALWAYS, DISPLAY_MODE_ON_FOCUS})

UI_FONT_SIZE_SMALL = "small"
UI_FONT_SIZE_MEDIUM = "medium"
UI_FONT_SIZE_LARGE = "large"
UI_FONT_SIZES = frozenset(
    {UI_FONT_SIZE_SMALL, UI_FONT_SIZE_MEDIUM, UI_FONT_SIZE_LARGE}
)
UI_FONT_POINT_SIZES = {
    UI_FONT_SIZE_SMALL: 9,
    UI_FONT_SIZE_MEDIUM: 11,
    UI_FONT_SIZE_LARGE: 14,
}


@dataclass
class AppSettings:
    color_jp: QColor
    color_en: QColor
    display_mode: str
    show_on_hover: bool
    ui_font_size: str


def default_settings() -> AppSettings:
    return AppSettings(
        color_jp=QColor(DEFAULT_COLOR_JP),
        color_en=QColor(DEFAULT_COLOR_EN),
        display_mode=DISPLAY_MODE_ALWAYS,
        show_on_hover=False,
        ui_font_size=UI_FONT_SIZE_MEDIUM,
    )


def default_colors() -> AppSettings:
    """Return defaults with colors only reset; display options come from current file if present."""
    current = load_settings()
    defaults = default_settings()
    return AppSettings(
        color_jp=defaults.color_jp,
        color_en=defaults.color_en,
        display_mode=current.display_mode,
        show_on_hover=current.show_on_hover,
        ui_font_size=current.ui_font_size,
    )


def config_dir() -> str:
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
        return os.path.join(base, "IMEAura")
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Application Support/IMEAura")
    xdg = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(xdg, "ime_aura")


def settings_path() -> str:
    return os.path.join(config_dir(), "settings.json")


def _color_to_list(color: QColor) -> list[int]:
    return [color.red(), color.green(), color.blue(), color.alpha()]


def _color_from_list(values: object, fallback: QColor) -> QColor:
    if not isinstance(values, (list, tuple)) or len(values) != 4:
        return QColor(fallback)
    try:
        r, g, b, a = (int(v) for v in values)
    except (TypeError, ValueError):
        return QColor(fallback)
    if not all(0 <= v <= 255 for v in (r, g, b, a)):
        return QColor(fallback)
    return QColor(r, g, b, a)


def _normalize_display_mode(value: object) -> str:
    if isinstance(value, str) and value in DISPLAY_MODES:
        return value
    return DISPLAY_MODE_ALWAYS


def _normalize_ui_font_size(value: object) -> str:
    if isinstance(value, str) and value in UI_FONT_SIZES:
        return value
    return UI_FONT_SIZE_MEDIUM


def ui_font_point_size(size_key: str) -> int:
    return UI_FONT_POINT_SIZES.get(
        _normalize_ui_font_size(size_key),
        UI_FONT_POINT_SIZES[UI_FONT_SIZE_MEDIUM],
    )


def _normalize_settings(data: dict) -> AppSettings:
    defaults = default_settings()
    display_mode = _normalize_display_mode(data.get("display_mode"))
    show_on_hover = bool(data.get("show_on_hover", False))
    # Hover is only valid with on-focus mode
    if display_mode != DISPLAY_MODE_ON_FOCUS:
        show_on_hover = False
    return AppSettings(
        color_jp=_color_from_list(data.get("color_jp"), defaults.color_jp),
        color_en=_color_from_list(data.get("color_en"), defaults.color_en),
        display_mode=display_mode,
        show_on_hover=show_on_hover,
        ui_font_size=_normalize_ui_font_size(data.get("ui_font_size")),
    )


def load_settings() -> AppSettings:
    path = settings_path()
    defaults = default_settings()
    if not os.path.isfile(path):
        return defaults

    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        logger.warning("Failed to load settings from %s: %s", path, exc)
        return defaults

    if not isinstance(data, dict):
        return defaults
    return _normalize_settings(data)


def save_settings(settings: AppSettings) -> None:
    path = settings_path()
    display_mode = _normalize_display_mode(settings.display_mode)
    show_on_hover = bool(settings.show_on_hover) and display_mode == DISPLAY_MODE_ON_FOCUS
    ui_font_size = _normalize_ui_font_size(settings.ui_font_size)
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        payload = {
            "color_jp": _color_to_list(settings.color_jp),
            "color_en": _color_to_list(settings.color_en),
            "display_mode": display_mode,
            "show_on_hover": show_on_hover,
            "ui_font_size": ui_font_size,
        }
        with open(path, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
            f.write("\n")
    except OSError as exc:
        logger.warning("Failed to save settings to %s: %s", path, exc)


# Backwards-compatible aliases used by older call sites
def load_colors() -> AppSettings:
    return load_settings()


def save_colors(color_jp: QColor, color_en: QColor) -> None:
    current = load_settings()
    save_settings(
        AppSettings(
            color_jp=color_jp,
            color_en=color_en,
            display_mode=current.display_mode,
            show_on_hover=current.show_on_hover,
            ui_font_size=current.ui_font_size,
        )
    )
