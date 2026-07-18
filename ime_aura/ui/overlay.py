"""Fullscreen edge-gradient overlay driven by IME state."""

from __future__ import annotations

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor, QLinearGradient, QPainter
from PySide6.QtWidgets import QApplication, QWidget

from ime_aura.platform.base import PlatformBackend, geometry_from_cursor
from ime_aura.settings import (
    DISPLAY_MODE_ALWAYS,
    DISPLAY_MODE_ON_FOCUS,
    AppSettings,
    default_colors,
    load_settings,
    save_settings,
)


class ImeOverlay(QWidget):
    def __init__(self, backend: PlatformBackend):
        super().__init__()
        self._backend = backend

        self.setWindowFlags(
            Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowTransparentForInput
            | Qt.WindowType.Tool
        )
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)

        settings = load_settings()
        self.color_jp = settings.color_jp
        self.color_en = settings.color_en
        self.display_mode = settings.display_mode
        self.show_on_hover = settings.show_on_hover
        self._gradient_visible = self._should_show_gradient()

        self.is_japanese = self._backend.is_japanese_input()

        app = QApplication.instance()
        geo = self._backend.get_active_screen_geometry(app) if app else None
        if geo:
            self.setGeometry(geo)
        elif app and app.primaryScreen():
            self.setGeometry(app.primaryScreen().geometry())

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.check_state)
        self.timer.start(100)

    def set_color_jp(self, color: QColor) -> None:
        self.color_jp = color
        self._persist_settings()
        self.update()

    def set_color_en(self, color: QColor) -> None:
        self.color_en = color
        self._persist_settings()
        self.update()

    def set_display_mode(self, mode: str) -> None:
        if mode == DISPLAY_MODE_ALWAYS:
            self.display_mode = DISPLAY_MODE_ALWAYS
            self.show_on_hover = False
        else:
            self.display_mode = DISPLAY_MODE_ON_FOCUS
        self._persist_settings()
        self._refresh_visibility()
        self.update()

    def set_show_on_hover(self, enabled: bool) -> None:
        if self.display_mode != DISPLAY_MODE_ON_FOCUS:
            self.show_on_hover = False
        else:
            self.show_on_hover = bool(enabled)
        self._persist_settings()
        self._refresh_visibility()
        self.update()

    def reset_colors_to_default(self) -> None:
        colors = default_colors()
        self.color_jp = colors.color_jp
        self.color_en = colors.color_en
        self._persist_settings()
        self.update()

    def _current_settings(self) -> AppSettings:
        return AppSettings(
            color_jp=self.color_jp,
            color_en=self.color_en,
            display_mode=self.display_mode,
            show_on_hover=self.show_on_hover,
        )

    def _persist_settings(self) -> None:
        save_settings(self._current_settings())

    def _visibility_state(self) -> tuple[bool, bool, bool]:
        """Return (should_show, is_focused, is_hovered)."""
        if self.display_mode == DISPLAY_MODE_ALWAYS:
            return True, False, False
        focused = self._backend.is_text_input_focused()
        hovered = False
        if self.show_on_hover:
            hovered = self._backend.is_text_input_hovered()
        return focused or hovered, focused, hovered

    def _should_show_gradient(self) -> bool:
        show, _, _ = self._visibility_state()
        return show

    def _target_screen_geometry(self, app: QApplication, focused: bool, hovered: bool):
        # Hover-only: follow the cursor's display. Focus/always: follow active window.
        if self.display_mode == DISPLAY_MODE_ON_FOCUS and hovered and not focused:
            return geometry_from_cursor(app)
        return self._backend.get_active_screen_geometry(app)

    def _refresh_visibility(self) -> None:
        self._gradient_visible = self._should_show_gradient()

    def check_state(self) -> None:
        app = QApplication.instance()
        if app is None:
            return

        new_state = self._backend.is_japanese_input()
        state_changed = new_state != self.is_japanese
        if state_changed:
            self.is_japanese = new_state

        show, focused, hovered = self._visibility_state()
        target_geo = self._target_screen_geometry(app, focused, hovered)
        geo_changed = False
        if target_geo and target_geo != self.geometry():
            self.setGeometry(target_geo)
            geo_changed = True

        visibility_changed = show != self._gradient_visible
        if visibility_changed:
            self._gradient_visible = show

        if state_changed or geo_changed or visibility_changed:
            self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        # Translucent windows keep the previous frame unless cleared explicitly.
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_Source)
        painter.fillRect(self.rect(), QColor(0, 0, 0, 0))

        if not self._gradient_visible:
            return

        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        base_color = self.color_jp if self.is_japanese else self.color_en
        transparent = QColor(0, 0, 0, 0)

        width = self.width()
        height = self.height()
        thickness = 15

        grad_top = QLinearGradient(0, 0, 0, thickness)
        grad_top.setColorAt(0, base_color)
        grad_top.setColorAt(1, transparent)
        painter.fillRect(0, 0, width, thickness, grad_top)

        grad_bottom = QLinearGradient(0, height, 0, height - thickness)
        grad_bottom.setColorAt(0, base_color)
        grad_bottom.setColorAt(1, transparent)
        painter.fillRect(0, height - thickness, width, thickness, grad_bottom)

        grad_left = QLinearGradient(0, 0, thickness, 0)
        grad_left.setColorAt(0, base_color)
        grad_left.setColorAt(1, transparent)
        painter.fillRect(0, 0, thickness, height, grad_left)

        grad_right = QLinearGradient(width, 0, width - thickness, 0)
        grad_right.setColorAt(0, base_color)
        grad_right.setColorAt(1, transparent)
        painter.fillRect(width - thickness, 0, thickness, height, grad_right)
