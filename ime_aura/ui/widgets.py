"""Minimal custom widgets for Signal Edge control surfaces."""

from __future__ import annotations

from PySide6.QtCore import (
    Property,
    QAbstractAnimation,
    QEasingCurve,
    QPointF,
    QPropertyAnimation,
    QRect,
    QRectF,
    Qt,
    QTimer,
    Signal,
)
from PySide6.QtGui import (
    QColor,
    QFont,
    QFontMetrics,
    QLinearGradient,
    QPainter,
    QPainterPath,
    QPen,
)
from PySide6.QtWidgets import (
    QButtonGroup,
    QFrame,
    QGraphicsOpacityEffect,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from ime_aura.ui import theme

_UNLIMITED_H = 16_777_215


def capsule_path(rect: QRectF) -> QPainterPath:
    """Stadium path: corner radius is 50% of height."""
    path = QPainterPath()
    if rect.width() <= 0 or rect.height() <= 0:
        return path
    radius = rect.height() * 0.5
    path.addRoundedRect(rect, radius, radius)
    return path


class Hairline(QFrame):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.setObjectName("Hairline")
        self.setFrameShape(QFrame.Shape.NoFrame)
        self.setFixedHeight(1)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)


class SectionRule(QWidget):
    """Hairline with vertical breathing room from the spacing scale."""

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, theme.SPACE_2, 0, theme.SPACE_2)
        layout.setSpacing(0)
        layout.addWidget(Hairline())
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)


class SectionHeader(QWidget):
    def __init__(
        self,
        title: str,
        hint: str = "",
        parent: QWidget | None = None,
    ):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(theme.SPACE_1)

        title_label = QLabel(title)
        title_label.setObjectName("SectionTitle")
        title_label.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        layout.addWidget(title_label)

        if hint:
            hint_label = QLabel(hint)
            hint_label.setObjectName("SectionHint")
            hint_label.setWordWrap(True)
            hint_label.setSizePolicy(
                QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed
            )
            layout.addWidget(hint_label)

        self.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)


class SquircleSwatch(QWidget):
    """Color preview with capsule ends (radius = 50% of height)."""

    clicked = Signal()

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self._color = QColor(theme.ACCENT_JP)
        self._display = QColor(theme.ACCENT_JP)
        self._hovered = False
        self._pressed = False
        self._scale = 1.0
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setAutoFillBackground(False)
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent, False)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        self.setToolTip("クリックして色を変更")
        self.apply_size_for_font(self.font())

        self._scale_anim = QPropertyAnimation(self, b"swatchScale", self)
        self._scale_anim.setDuration(theme.motion_ms(theme.HOVER_MS))
        self._scale_anim.setEasingCurve(QEasingCurve.Type.OutCubic)

        self._color_anim = QPropertyAnimation(self, b"displayColor", self)
        self._color_anim.setDuration(theme.motion_ms(theme.STATUS_BLEND_MS))
        self._color_anim.setEasingCurve(QEasingCurve.Type.InOutCubic)

    def apply_size_for_font(self, font: QFont) -> None:
        """Keep padding fixed so the chip grows and shrinks with the UI font."""
        line = QFontMetrics(font).height()
        height = max(theme.HIT_MIN, line + theme.BUTTON_PAD_Y * 2)
        width = max(
            1,
            int(round(height * theme.SWATCH_WIDTH / theme.SWATCH_HEIGHT)),
        )
        self.setFixedSize(width, height)

    def color(self) -> QColor:
        return QColor(self._color)

    def setColor(self, color: QColor, animate: bool = True) -> None:
        self._color = QColor(color)
        duration = theme.motion_ms(theme.STATUS_BLEND_MS) if animate else 0
        if duration <= 0 or self._display == self._color:
            self._display = QColor(self._color)
            self.update()
            return
        self._color_anim.stop()
        self._color_anim.setDuration(duration)
        self._color_anim.setStartValue(self._display)
        self._color_anim.setEndValue(QColor(self._color))
        self._color_anim.start()

    def _get_scale(self) -> float:
        return self._scale

    def _set_scale(self, value: float) -> None:
        self._scale = float(value)
        self.update()

    swatchScale = Property(float, _get_scale, _set_scale)

    def _get_display(self) -> QColor:
        return QColor(self._display)

    def _set_display(self, color: QColor) -> None:
        self._display = QColor(color)
        self.update()

    displayColor = Property(QColor, _get_display, _set_display)

    def _animate_scale(self, target: float) -> None:
        duration = theme.motion_ms(theme.HOVER_MS)
        if duration <= 0:
            self._scale = target
            self.update()
            return
        self._scale_anim.stop()
        self._scale_anim.setDuration(duration)
        self._scale_anim.setStartValue(self._scale)
        self._scale_anim.setEndValue(target)
        self._scale_anim.start()

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self._pressed = True
            self._animate_scale(0.96)
        super().mousePressEvent(event)

    def mouseReleaseEvent(self, event) -> None:
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._pressed
            and self.rect().contains(event.position().toPoint())
        ):
            self.clicked.emit()
        self._pressed = False
        self._animate_scale(1.04 if self._hovered else 1.0)
        super().mouseReleaseEvent(event)

    def enterEvent(self, event) -> None:
        self._hovered = True
        if not self._pressed:
            self._animate_scale(1.04)
        super().enterEvent(event)

    def leaveEvent(self, event) -> None:
        self._hovered = False
        self._pressed = False
        self._animate_scale(1.0)
        super().leaveEvent(event)

    def keyPressEvent(self, event) -> None:
        if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter, Qt.Key.Key_Space):
            self.clicked.emit()
            return
        super().keyPressEvent(event)

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        inset = 2.0 if self.hasFocus() else 1.0
        grow = (self._scale - 1.0) * min(self.width(), self.height()) * 0.5
        rect = QRectF(self.rect()).adjusted(
            inset - grow, inset - grow, -inset + grow, -inset + grow
        )
        path = capsule_path(rect)

        painter.save()
        painter.setClipPath(path)
        cell = 6
        origin = rect.topLeft()
        y = origin.y()
        row = 0
        while y < rect.bottom():
            x = origin.x()
            col = 0
            while x < rect.right():
                shade = theme.CHECKER_LIGHT if (row + col) % 2 == 0 else theme.CHECKER_DARK
                painter.fillRect(QRectF(x, y, cell, cell), shade)
                x += cell
                col += 1
            y += cell
            row += 1
        painter.restore()

        fill = QColor(self._display)
        if self._pressed:
            fill.setAlpha(max(40, int(fill.alpha() * 0.88)))
        painter.fillPath(path, fill)

        if self._hovered and not self._pressed:
            wash = QColor(255, 255, 255, 36)
            painter.fillPath(path, wash)

        border = QColor(theme.FOCUS_RING) if self.hasFocus() else QColor(255, 255, 255, 140)
        if not self.hasFocus() and self._hovered:
            border = QColor(theme.FOCUS_RING)
            border.setAlpha(120)
        painter.setPen(QPen(border, 2.0 if self.hasFocus() else 1.0))
        painter.drawPath(path)

        # Trailing chevron: the chip is a control, not a static fill.
        painter.setClipPath(path)
        chevron = QColor(255, 255, 255, 220)
        shadow = QColor(0, 0, 0, 50)
        mid_y = rect.center().y()
        x = rect.right() - max(8.0, rect.height() * 0.28)
        for color, dx in ((shadow, 0.6), (chevron, 0.0)):
            painter.setPen(
                QPen(
                    color,
                    1.6,
                    Qt.PenStyle.SolidLine,
                    Qt.PenCapStyle.RoundCap,
                    Qt.PenJoinStyle.RoundJoin,
                )
            )
            painter.drawLine(
                QPointF(x - 3.2 + dx, mid_y - 4.2),
                QPointF(x + 1.2 + dx, mid_y),
            )
            painter.drawLine(
                QPointF(x + 1.2 + dx, mid_y),
                QPointF(x - 3.2 + dx, mid_y + 4.2),
            )


class FeedbackButton(QPushButton):
    """Button with hover/press opacity and a brief status flash after actions."""

    def __init__(
        self,
        text: str = "",
        parent: QWidget | None = None,
        *,
        variant: str = "secondary",
    ):
        super().__init__(text, parent)
        self._variant = variant
        if variant == "quiet":
            self.setObjectName("QuietButton")
        elif variant == "danger":
            self.setObjectName("DangerButton")
        else:
            self.setObjectName("SecondaryButton")
        self._label = text
        self._opacity = 1.0
        effect = QGraphicsOpacityEffect(self)
        effect.setOpacity(1.0)
        self.setGraphicsEffect(effect)
        self._effect = effect
        self._anim = QPropertyAnimation(self, b"pressOpacity", self)
        self._anim.setDuration(theme.motion_ms(theme.BUTTON_PRESS_MS))
        self._anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._flash_timer = QTimer(self)
        self._flash_timer.setSingleShot(True)
        self._flash_timer.timeout.connect(self._restore_label)

    def _get_opacity(self) -> float:
        return self._opacity

    def _set_opacity(self, value: float) -> None:
        self._opacity = float(value)
        self._effect.setOpacity(self._opacity)

    pressOpacity = Property(float, _get_opacity, _set_opacity)

    def _animate_opacity(self, target: float) -> None:
        duration = theme.motion_ms(theme.BUTTON_PRESS_MS)
        if duration <= 0:
            self._opacity = target
            self._effect.setOpacity(target)
            return
        self._anim.stop()
        self._anim.setDuration(duration)
        self._anim.setStartValue(self._opacity)
        self._anim.setEndValue(target)
        self._anim.start()

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self._animate_opacity(0.72)
        super().mousePressEvent(event)

    def mouseReleaseEvent(self, event) -> None:
        self._animate_opacity(1.0)
        super().mouseReleaseEvent(event)

    def leaveEvent(self, event) -> None:
        if self._opacity != 1.0:
            self._animate_opacity(1.0)
        super().leaveEvent(event)

    def flash_status(self, message: str) -> None:
        self._flash_timer.stop()
        self.setText(message)
        self._flash_timer.start(theme.STATUS_FLASH_MS)

    def _restore_label(self) -> None:
        self.setText(self._label)


class RevealPanel(QWidget):
    """Expand/collapse a child row with height (instant if reduced motion)."""

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self._expanded = True
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.setMinimumHeight(0)
        self.setMaximumHeight(_UNLIMITED_H)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)

        inner_layout = QVBoxLayout(self)
        inner_layout.setContentsMargins(theme.SPACE_4, 0, 0, 0)
        inner_layout.setSpacing(0)

        self._h_anim = QPropertyAnimation(self, b"maximumHeight", self)
        self._h_anim.setEasingCurve(QEasingCurve.Type.InOutCubic)
        self._h_anim.finished.connect(self._on_height_finished)

    def add_widget(self, widget: QWidget) -> None:
        widget.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.layout().addWidget(widget)

    def is_expanded(self) -> bool:
        return self._expanded

    def _content_hint(self) -> int:
        self.updateGeometry()
        return max(theme.HIT_MIN, self.sizeHint().height())

    def set_expanded(self, expanded: bool, animate: bool = True) -> None:
        expanded = bool(expanded)
        if expanded == self._expanded and animate:
            return
        self._expanded = expanded
        hint = self._content_hint()
        duration = theme.motion_ms(theme.REVEAL_MS) if animate else 0
        if duration <= 0:
            self._h_anim.stop()
            self.setMaximumHeight(_UNLIMITED_H if expanded else 0)
            return

        self._h_anim.stop()
        self._h_anim.setDuration(duration)
        current_h = self.maximumHeight()
        if current_h >= _UNLIMITED_H // 2:
            current_h = hint if not expanded else 0
        start_h = current_h if current_h > 0 or not expanded else 0
        if expanded and start_h <= 0:
            start_h = 0
        self.setMaximumHeight(start_h)
        self._h_anim.setStartValue(start_h)
        self._h_anim.setEndValue(hint if expanded else 0)
        self._h_anim.start()

    def refresh_expanded_height(self) -> None:
        if self._expanded and self._h_anim.state() == QAbstractAnimation.State.Stopped:
            self.setMaximumHeight(_UNLIMITED_H)

    def _on_height_finished(self) -> None:
        if self._expanded:
            self.setMaximumHeight(_UNLIMITED_H)


class SegmentedControl(QWidget):
    """Exclusive text-size choices with a sliding squircle indicator."""

    indexChanged = Signal(int)

    def __init__(self, labels: list[str], parent: QWidget | None = None):
        super().__init__(parent)
        self.setObjectName("SegmentedTrack")
        self._index = 0
        self._ready = False

        self._indicator = QWidget(self)
        self._indicator.setObjectName("SegmentedIndicator")
        self._indicator.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self._indicator.lower()

        self._anim = QPropertyAnimation(self._indicator, b"geometry", self)
        self._anim.setEasingCurve(QEasingCurve.Type.OutCubic)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(theme.SPACE_1, theme.SPACE_1, theme.SPACE_1, theme.SPACE_1)
        layout.setSpacing(0)

        self._group = QButtonGroup(self)
        self._group.setExclusive(True)
        self._buttons: list[QPushButton] = []
        for i, label in enumerate(labels):
            btn = QPushButton(label)
            btn.setObjectName("SegmentedButton")
            btn.setCheckable(True)
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
            btn.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            btn.clicked.connect(lambda checked, idx=i: self._on_clicked(idx, checked))
            self._group.addButton(btn, i)
            layout.addWidget(btn)
            self._buttons.append(btn)

        if self._buttons:
            self._buttons[0].setChecked(True)
        self._indicator.lower()

    def set_fonts(self, fonts: list[QFont]) -> None:
        for btn, font in zip(self._buttons, fonts):
            btn.setFont(font)

    def index(self) -> int:
        return self._index

    def set_index(self, index: int, animate: bool = True) -> None:
        if not self._buttons:
            return
        index = max(0, min(index, len(self._buttons) - 1))
        self._index = index
        btn = self._buttons[index]
        if not btn.isChecked():
            btn.setChecked(True)
        self._move_indicator(animate=animate)

    def _on_clicked(self, index: int, checked: bool) -> None:
        if not checked:
            return
        if index == self._index:
            self._move_indicator(animate=True)
            return
        self._index = index
        self._move_indicator(animate=True)
        self.indexChanged.emit(index)

    def _indicator_rect(self) -> QRect:
        btn = self._buttons[self._index]
        return btn.geometry()

    def _move_indicator(self, animate: bool) -> None:
        if not self._buttons:
            return
        target = self._indicator_rect()
        if not target.isValid() or target.width() <= 0:
            return
        duration = theme.motion_ms(theme.INDICATOR_MS) if animate and self._ready else 0
        if duration <= 0:
            self._anim.stop()
            self._indicator.setGeometry(target)
            return
        self._anim.stop()
        self._anim.setDuration(duration)
        self._anim.setStartValue(self._indicator.geometry())
        self._anim.setEndValue(target)
        self._anim.start()

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        self._move_indicator(animate=False)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        self._move_indicator(animate=False)
        self._ready = True


def paint_atmosphere(widget: QWidget) -> None:
    """Paint Signal Edge wash onto a widget background (call from paintEvent)."""
    painter = QPainter(widget)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.fillRect(widget.rect(), theme.BG)

    grad = QLinearGradient(QPointF(0, 0), QPointF(widget.width(), widget.height()))
    grad.setColorAt(0.0, theme.BG_WASH_JP)
    grad.setColorAt(0.42, QColor(248, 249, 252, 0))
    grad.setColorAt(1.0, theme.BG_WASH_EN)
    painter.fillRect(widget.rect(), grad)
