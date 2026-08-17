"""Fullscreen edge-gradient overlay driven by IME state."""

from __future__ import annotations

from PySide6.QtCore import Property, QEasingCurve, QMetaObject, QPropertyAnimation, Qt, QTimer, Slot
from PySide6.QtGui import QBrush, QColor, QLinearGradient, QPainter
from PySide6.QtWidgets import QApplication, QWidget

from ime_aura.platform.base import PlatformBackend, geometry_from_cursor
from ime_aura.settings import (
    DISPLAY_MODE_ALWAYS,
    DISPLAY_MODE_HIDDEN,
    DISPLAY_MODE_ON_FOCUS,
    AppSettings,
    default_colors,
    load_settings,
    save_settings,
)
from ime_aura.ui import theme


def _ease_out_alpha_stops(base: QColor) -> list[tuple[float, QColor]]:
    """Soft linear falloff from solid edge to transparent."""
    opaque = QColor(base)
    mid = QColor(base)
    mid.setAlpha(int(round(base.alpha() * 0.35)))
    clear = QColor(base)
    clear.setAlpha(0)
    return [(0.0, opaque), (0.55, mid), (1.0, clear)]


def _apply_stops(grad: QLinearGradient, base: QColor) -> None:
    for pos, color in _ease_out_alpha_stops(base):
        grad.setColorAt(pos, color)


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
        self.ui_font_size = settings.ui_font_size
        self.gradient_width = settings.gradient_width
        self._gradient_visible = self._should_show_gradient()
        self._fade = 1.0 if self._gradient_visible else 0.0
        self._fade_anim: QPropertyAnimation | None = None
        self._blend_color = QColor(
            self.color_jp if self._backend.is_japanese_input() else self.color_en
        )
        self._color_anim: QPropertyAnimation | None = None

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
        if hasattr(self._backend, "set_text_input_changed_callback"):
            self._backend.set_text_input_changed_callback(self._on_backend_text_input_changed)

    def _get_fade(self) -> float:
        return self._fade

    def _set_fade(self, value: float) -> None:
        self._fade = max(0.0, min(1.0, float(value)))
        self.update()

    fadeOpacity = Property(float, _get_fade, _set_fade)

    def _get_blend_color(self) -> QColor:
        return self._blend_color

    def _set_blend_color(self, color: QColor) -> None:
        self._blend_color = QColor(color)
        self.update()

    blendColor = Property(QColor, _get_blend_color, _set_blend_color)

    def set_color_jp(self, color: QColor) -> None:
        self.color_jp = color
        self._persist_settings()
        if self.is_japanese:
            self._animate_color_to(color)
        else:
            self.update()

    def set_color_en(self, color: QColor) -> None:
        self.color_en = color
        self._persist_settings()
        if not self.is_japanese:
            self._animate_color_to(color)
        else:
            self.update()

    def set_display_mode(self, mode: str) -> None:
        if mode == DISPLAY_MODE_ALWAYS:
            self.display_mode = DISPLAY_MODE_ALWAYS
            self.show_on_hover = False
        elif mode == DISPLAY_MODE_HIDDEN:
            self.display_mode = DISPLAY_MODE_HIDDEN
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

    def set_ui_font_size(self, size_key: str) -> None:
        from ime_aura.settings import UI_FONT_SIZE_MEDIUM, UI_FONT_SIZES

        self.ui_font_size = size_key if size_key in UI_FONT_SIZES else UI_FONT_SIZE_MEDIUM
        self._persist_settings()

    def set_gradient_width(self, width: int) -> None:
        from ime_aura.settings import _normalize_gradient_width

        self.gradient_width = _normalize_gradient_width(width)
        self._persist_settings()
        self.update()

    def reset_colors_to_default(self) -> None:
        colors = default_colors()
        self.color_jp = colors.color_jp
        self.color_en = colors.color_en
        self._persist_settings()
        self._animate_color_to(self.color_jp if self.is_japanese else self.color_en)

    def _current_settings(self) -> AppSettings:
        return AppSettings(
            color_jp=self.color_jp,
            color_en=self.color_en,
            display_mode=self.display_mode,
            show_on_hover=self.show_on_hover,
            ui_font_size=self.ui_font_size,
            gradient_width=self.gradient_width,
        )

    def _persist_settings(self) -> None:
        save_settings(self._current_settings())

    def _visibility_state(self) -> tuple[bool, bool, bool]:
        """Return (should_show, is_focused, is_hovered)."""
        if self.display_mode == DISPLAY_MODE_HIDDEN:
            return False, False, False
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

    def _animate_fade_to(self, target: float) -> None:
        if self._fade_anim is not None:
            self._fade_anim.stop()
        self._fade_anim = QPropertyAnimation(self, b"fadeOpacity", self)
        self._fade_anim.setDuration(theme.motion_ms(theme.FADE_MS))
        self._fade_anim.setStartValue(self._fade)
        self._fade_anim.setEndValue(target)
        self._fade_anim.setEasingCurve(QEasingCurve.Type.InOutCubic)
        self._fade_anim.start()

    def _animate_color_to(self, target: QColor) -> None:
        if self._color_anim is not None:
            self._color_anim.stop()
        self._color_anim = QPropertyAnimation(self, b"blendColor", self)
        self._color_anim.setDuration(theme.motion_ms(theme.STATUS_BLEND_MS))
        self._color_anim.setStartValue(self._blend_color)
        self._color_anim.setEndValue(QColor(target))
        self._color_anim.setEasingCurve(QEasingCurve.Type.InOutCubic)
        self._color_anim.start()

    def _refresh_visibility(self) -> None:
        show = self._should_show_gradient()
        if show != self._gradient_visible:
            self._gradient_visible = show
            self._animate_fade_to(1.0 if show else 0.0)

    def _on_backend_text_input_changed(self) -> None:
        QMetaObject.invokeMethod(self, "check_state", Qt.ConnectionType.QueuedConnection)

    @Slot()
    def check_state(self) -> None:
        app = QApplication.instance()
        if app is None:
            return

        new_state = self._backend.is_japanese_input()
        state_changed = new_state != self.is_japanese
        if state_changed:
            self.is_japanese = new_state
            self._animate_color_to(self.color_jp if new_state else self.color_en)

        show, focused, hovered = self._visibility_state()
        target_geo = self._target_screen_geometry(app, focused, hovered)
        geo_changed = False
        if target_geo and target_geo != self.geometry():
            self.setGeometry(target_geo)
            geo_changed = True

        visibility_changed = show != self._gradient_visible
        if visibility_changed:
            self._gradient_visible = show
            self._animate_fade_to(1.0 if show else 0.0)

        if state_changed or geo_changed:
            self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_Source)
        painter.fillRect(self.rect(), QColor(0, 0, 0, 0))

        if self._fade <= 0.001:
            return

        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        base_color = QColor(self._blend_color)
        base_color.setAlpha(int(round(base_color.alpha() * self._fade)))

        width = self.width()
        height = self.height()
        # Keep a clean frame even when the user picks a very large width.
        thickness = max(1, min(self.gradient_width, max(1, min(width, height) // 2)))

        # Full-edge strips (original reliable look). Soft multi-stop falloff
        # keeps corners readable without radial corner blobs.
        top = QLinearGradient(0, 0, 0, thickness)
        _apply_stops(top, base_color)
        painter.fillRect(0, 0, width, thickness, QBrush(top))

        bottom = QLinearGradient(0, height, 0, height - thickness)
        _apply_stops(bottom, base_color)
        painter.fillRect(0, height - thickness, width, thickness, QBrush(bottom))

        left = QLinearGradient(0, 0, thickness, 0)
        _apply_stops(left, base_color)
        painter.fillRect(0, 0, thickness, height, QBrush(left))

        right = QLinearGradient(width, 0, width - thickness, 0)
        _apply_stops(right, base_color)
        painter.fillRect(width - thickness, 0, thickness, height, QBrush(right))
