"""Control window for color customization and quitting."""

from __future__ import annotations

from PySide6.QtCore import QEasingCurve, QPropertyAnimation, Qt, QTimer
from PySide6.QtGui import QFont, QFontMetrics
from PySide6.QtWidgets import (
    QAbstractSpinBox,
    QApplication,
    QButtonGroup,
    QCheckBox,
    QColorDialog,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QRadioButton,
    QScrollArea,
    QSizePolicy,
    QSlider,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ime_aura.settings import (
    DEFAULT_GRADIENT_WIDTH,
    DISPLAY_MODE_ALWAYS,
    DISPLAY_MODE_ON_FOCUS,
    GRADIENT_WIDTH_MAX,
    GRADIENT_WIDTH_MIN,
    UI_FONT_SIZE_LARGE,
    UI_FONT_SIZE_MEDIUM,
    UI_FONT_SIZE_SMALL,
    ui_font_point_size,
)
from ime_aura.ui import theme
from ime_aura.ui.about_dialog import AboutDialog
from ime_aura.ui.icons import load_svg_icon
from ime_aura.ui.overlay import ImeOverlay
from ime_aura.ui.widgets import (
    FeedbackButton,
    RevealPanel,
    SectionHeader,
    SectionRule,
    SegmentedControl,
    SquircleSwatch,
    paint_atmosphere,
)


def _no_vshrink(widget: QWidget) -> None:
    widget.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)


def _pick_color(parent: QWidget, initial, title: str):
    dialog = QColorDialog(initial, parent)
    dialog.setWindowTitle(title)
    dialog.setOption(QColorDialog.ColorDialogOption.ShowAlphaChannel, True)
    dialog.setStyleSheet(theme.color_dialog_stylesheet())
    dialog.adjustSize()
    if dialog.exec():
        color = dialog.currentColor()
        if color.isValid():
            return color
    return None


class ControlWindow(QWidget):
    def __init__(self, overlay: ImeOverlay):
        super().__init__()
        self.overlay = overlay
        self._quitting = False
        self.setObjectName("ControlRoot")
        self.setWindowTitle("IME Aura")
        self.setWindowIcon(load_svg_icon())
        self.setWindowFlag(Qt.WindowType.WindowMaximizeButtonHint, False)
        self.setMinimumSize(theme.MIN_WINDOW_WIDTH, theme.MIN_WINDOW_HEIGHT)
        self.resize(theme.MIN_WINDOW_WIDTH, theme.MIN_WINDOW_HEIGHT)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self._entrance_played = False
        if theme.motion_ms(theme.ENTRANCE_MS) > 0:
            self.setWindowOpacity(0.0)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self._scroll = QScrollArea()
        self._scroll.setObjectName("ControlScroll")
        self._scroll.setWidgetResizable(False)
        self._scroll.setFrameShape(QScrollArea.Shape.NoFrame)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self._scroll.setAlignment(Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignLeft)

        self._content = QWidget()
        self._content.setObjectName("ControlContent")
        self._content.setStyleSheet(
            "QWidget#ControlContent { background: transparent; }"
        )
        root = QVBoxLayout(self._content)
        root.setContentsMargins(theme.MARGIN, theme.MARGIN, theme.MARGIN, theme.MARGIN)
        root.setSpacing(theme.ROW_GAP)
        root.setAlignment(Qt.AlignmentFlag.AlignTop)

        # --- Colors ---
        colors_header = SectionHeader("色", "クリックして画面縁の色を変更します")
        _no_vshrink(colors_header)
        root.addWidget(colors_header)

        colors_col = QVBoxLayout()
        colors_col.setContentsMargins(0, 0, 0, 0)
        colors_col.setSpacing(theme.ROW_GAP)

        jp_row = QHBoxLayout()
        jp_row.setContentsMargins(0, 0, 0, 0)
        jp_row.setSpacing(theme.GAP)
        self.jp_label = QLabel("日本語")
        _no_vshrink(self.jp_label)
        self.jp_swatch = SquircleSwatch()
        self.jp_swatch.setAccessibleName("日本語入力時の色")
        self.jp_swatch.setColor(self.overlay.color_jp, animate=False)
        self.jp_swatch.clicked.connect(self.choose_jp_color)
        jp_row.addWidget(self.jp_label, 0, Qt.AlignmentFlag.AlignVCenter)
        jp_row.addStretch(1)
        jp_row.addWidget(self.jp_swatch, 0, Qt.AlignmentFlag.AlignVCenter)
        colors_col.addLayout(jp_row)

        en_row = QHBoxLayout()
        en_row.setContentsMargins(0, 0, 0, 0)
        en_row.setSpacing(theme.GAP)
        self.en_label = QLabel("英語")
        _no_vshrink(self.en_label)
        self.en_swatch = SquircleSwatch()
        self.en_swatch.setAccessibleName("英語入力時の色")
        self.en_swatch.setColor(self.overlay.color_en, animate=False)
        self.en_swatch.clicked.connect(self.choose_en_color)
        en_row.addWidget(self.en_label, 0, Qt.AlignmentFlag.AlignVCenter)
        en_row.addStretch(1)
        en_row.addWidget(self.en_swatch, 0, Qt.AlignmentFlag.AlignVCenter)
        colors_col.addLayout(en_row)

        self.reset_colors_btn = FeedbackButton("デフォルトの色に戻す", variant="quiet")
        self.reset_colors_btn.setMinimumWidth(0)
        self.reset_colors_btn.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        self.reset_colors_btn.clicked.connect(self.reset_colors)
        colors_col.addWidget(self.reset_colors_btn)
        root.addLayout(colors_col)
        root.addWidget(SectionRule())

        # --- Width ---
        width_header = SectionHeader(
            "グラデーションの幅", "画面縁の帯の厚さ (1-100 px)"
        )
        _no_vshrink(width_header)
        root.addWidget(width_header)
        width_col = QVBoxLayout()
        width_col.setContentsMargins(0, 0, 0, 0)
        width_col.setSpacing(theme.ROW_GAP)

        width_row = QHBoxLayout()
        width_row.setContentsMargins(0, 0, 0, 0)
        width_row.setSpacing(theme.GAP)
        self.width_slider = QSlider(Qt.Orientation.Horizontal)
        self.width_slider.setRange(GRADIENT_WIDTH_MIN, GRADIENT_WIDTH_MAX)
        self.width_slider.setMinimumWidth(80)
        self.width_slider.setValue(self.overlay.gradient_width)
        self.width_slider.setToolTip("帯の厚さをドラッグして調整")
        width_row.addWidget(self.width_slider, 1)

        self.width_spin = QSpinBox()
        self.width_spin.setRange(GRADIENT_WIDTH_MIN, GRADIENT_WIDTH_MAX)
        self.width_spin.setButtonSymbols(QAbstractSpinBox.ButtonSymbols.NoButtons)
        self.width_spin.setSuffix(" px")
        self.width_spin.setValue(self.overlay.gradient_width)
        self.width_spin.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.width_spin.setToolTip("帯の厚さ (ピクセル)")
        width_row.addWidget(self.width_spin, 0, Qt.AlignmentFlag.AlignVCenter)
        width_col.addLayout(width_row)

        self.reset_width_btn = FeedbackButton("デフォルトの幅に戻す", variant="quiet")
        self.reset_width_btn.setMinimumWidth(0)
        self.reset_width_btn.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        self.reset_width_btn.clicked.connect(self.reset_gradient_width)
        width_col.addWidget(self.reset_width_btn)
        root.addLayout(width_col)
        root.addWidget(SectionRule())

        # --- Display ---
        display_header = SectionHeader("グラデーション表示")
        _no_vshrink(display_header)
        root.addWidget(display_header)
        display_col = QVBoxLayout()
        display_col.setContentsMargins(0, 0, 0, 0)
        display_col.setSpacing(theme.SPACE_1)
        self.radio_always = QRadioButton("常に表示")
        self.radio_on_focus = QRadioButton("テキスト入力時のみ")
        self.display_group = QButtonGroup(self)
        self.display_group.addButton(self.radio_always)
        self.display_group.addButton(self.radio_on_focus)
        display_col.addWidget(self.radio_always)
        display_col.addWidget(self.radio_on_focus)

        self.hover_check = QCheckBox("テキストボックスへホバー時も表示")
        self.hover_reveal = RevealPanel()
        self.hover_reveal.add_widget(self.hover_check)
        display_col.addWidget(self.hover_reveal)
        root.addLayout(display_col)
        root.addWidget(SectionRule())

        # --- Type ---
        type_header = SectionHeader("文字サイズ", "このウィンドウの文字の大きさ")
        _no_vshrink(type_header)
        root.addWidget(type_header)
        self.font_segment = SegmentedControl(["小", "中", "大"])
        self.font_segment.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        root.addWidget(self.font_segment)
        root.addWidget(SectionRule())

        actions_col = QVBoxLayout()
        actions_col.setContentsMargins(0, 0, 0, 0)
        actions_col.setSpacing(theme.ROW_GAP)
        self.about_btn = FeedbackButton("バージョン情報...")
        self.about_btn.setMinimumWidth(0)
        self.about_btn.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        self.about_btn.clicked.connect(self._show_about)
        actions_col.addWidget(self.about_btn)

        self.exit_btn = FeedbackButton("アプリケーションを終了", variant="danger")
        self.exit_btn.setMinimumWidth(0)
        self.exit_btn.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        self.exit_btn.clicked.connect(self._request_quit)
        actions_col.addWidget(self.exit_btn)
        root.addLayout(actions_col)

        self._content.setSizePolicy(
            QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Maximum
        )

        self._scroll.setWidget(self._content)
        outer.addWidget(self._scroll)

        if self.overlay.display_mode == DISPLAY_MODE_ON_FOCUS:
            self.radio_on_focus.setChecked(True)
        else:
            self.radio_always.setChecked(True)
        self.hover_check.setChecked(self.overlay.show_on_hover)
        self.hover_check.setEnabled(self.radio_on_focus.isChecked())
        self.hover_reveal.set_expanded(
            self.radio_on_focus.isChecked(), animate=False
        )

        font_size = self.overlay.ui_font_size
        if font_size == UI_FONT_SIZE_SMALL:
            self.font_segment.set_index(0, animate=False)
        elif font_size == UI_FONT_SIZE_LARGE:
            self.font_segment.set_index(2, animate=False)
        else:
            self.font_segment.set_index(1, animate=False)

        self.radio_always.toggled.connect(self._on_display_mode_changed)
        self.radio_on_focus.toggled.connect(self._on_display_mode_changed)
        self.hover_check.toggled.connect(self._on_hover_toggled)
        self.font_segment.indexChanged.connect(self._on_font_index_changed)
        self.width_slider.valueChanged.connect(self._on_width_slider_changed)
        self.width_spin.valueChanged.connect(self._on_width_spin_changed)

        self._apply_ui_font_size(self.overlay.ui_font_size)
        QTimer.singleShot(0, self._sync_content_width)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        if not self._entrance_played:
            self._entrance_played = True
            QTimer.singleShot(0, self._play_entrance)

    def _play_entrance(self) -> None:
        duration = theme.motion_ms(theme.ENTRANCE_MS)
        if duration <= 0:
            self.setWindowOpacity(1.0)
            return
        self.setWindowOpacity(0.0)
        anim = QPropertyAnimation(self, b"windowOpacity", self)
        anim.setDuration(duration)
        anim.setStartValue(0.0)
        anim.setEndValue(1.0)
        anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._entrance_anim = anim
        anim.start()

    def _sync_content_width(self) -> None:
        # Reserve a gutter for the scrollbar; height follows content sizeHint.
        area_w = max(0, self._scroll.width() - theme.SCROLLBAR_GUTTER)
        if area_w <= 0:
            return
        self._content.setFixedWidth(area_w)
        self._content.adjustSize()
        hint_h = self._content.sizeHint().height()
        self._content.setFixedHeight(max(hint_h, 1))

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        self._sync_content_width()

    def paintEvent(self, event) -> None:
        paint_atmosphere(self)
        super().paintEvent(event)

    def _show_about(self) -> None:
        AboutDialog(self).exec()

    def _apply_ui_font_size(self, size_key: str) -> None:
        font = theme.apply_app_font(ui_font_point_size(size_key))
        self.setFont(font)
        for widget in self.findChildren(QWidget):
            if widget is self.jp_swatch or widget is self.en_swatch:
                continue
            if widget.objectName() == "SegmentedButton":
                continue
            widget.setFont(font)

        preview_fonts = []
        for key in (UI_FONT_SIZE_SMALL, UI_FONT_SIZE_MEDIUM, UI_FONT_SIZE_LARGE):
            preview = QFont(font)
            preview.setPointSize(ui_font_point_size(key))
            preview_fonts.append(preview)
        self.font_segment.set_fonts(preview_fonts)

        fm = QFontMetrics(font)
        label_w = max(fm.horizontalAdvance("日本語"), fm.horizontalAdvance("英語"))
        self.jp_label.setMinimumWidth(label_w)
        self.en_label.setMinimumWidth(label_w)
        self.jp_swatch.apply_size_for_font(font)
        self.en_swatch.apply_size_for_font(font)
        self.width_spin.setFixedWidth(
            fm.horizontalAdvance("100 px") + theme.BUTTON_PAD_X * 2 + theme.GAP
        )
        self.hover_reveal.refresh_expanded_height()
        self._sync_content_width()

    def _on_font_index_changed(self, index: int) -> None:
        keys = (UI_FONT_SIZE_SMALL, UI_FONT_SIZE_MEDIUM, UI_FONT_SIZE_LARGE)
        size_key = keys[max(0, min(index, len(keys) - 1))]
        self.overlay.set_ui_font_size(size_key)
        self._apply_ui_font_size(size_key)

    def _set_width_controls(self, value: int) -> None:
        self.width_slider.blockSignals(True)
        self.width_spin.blockSignals(True)
        self.width_slider.setValue(value)
        self.width_spin.setValue(value)
        self.width_slider.blockSignals(False)
        self.width_spin.blockSignals(False)

    def _on_width_slider_changed(self, value: int) -> None:
        self.width_spin.blockSignals(True)
        self.width_spin.setValue(value)
        self.width_spin.blockSignals(False)
        self.overlay.set_gradient_width(value)

    def _on_width_spin_changed(self, value: int) -> None:
        self.width_slider.blockSignals(True)
        self.width_slider.setValue(value)
        self.width_slider.blockSignals(False)
        self.overlay.set_gradient_width(value)

    def reset_gradient_width(self) -> None:
        self.overlay.set_gradient_width(DEFAULT_GRADIENT_WIDTH)
        self._set_width_controls(self.overlay.gradient_width)
        self.reset_width_btn.flash_status("戻しました")

    def _on_display_mode_changed(self, checked: bool) -> None:
        if not checked:
            return
        if self.radio_always.isChecked():
            self.overlay.set_display_mode(DISPLAY_MODE_ALWAYS)
            self.hover_check.blockSignals(True)
            self.hover_check.setChecked(False)
            self.hover_check.blockSignals(False)
            self.hover_check.setEnabled(False)
            self.hover_reveal.set_expanded(False)
        else:
            self.overlay.set_display_mode(DISPLAY_MODE_ON_FOCUS)
            self.hover_check.setEnabled(True)
            self.hover_reveal.set_expanded(True)
            self.overlay.set_show_on_hover(self.hover_check.isChecked())
        QTimer.singleShot(theme.motion_ms(theme.REVEAL_MS), self._sync_content_width)

    def _on_hover_toggled(self, checked: bool) -> None:
        self.overlay.set_show_on_hover(checked)

    def choose_jp_color(self) -> None:
        color = _pick_color(self, self.overlay.color_jp, "日本語入力時の色を選択")
        if color is not None:
            self.overlay.set_color_jp(color)
            self.jp_swatch.setColor(color)

    def choose_en_color(self) -> None:
        color = _pick_color(self, self.overlay.color_en, "英語入力時の色を選択")
        if color is not None:
            self.overlay.set_color_en(color)
            self.en_swatch.setColor(color)

    def reset_colors(self) -> None:
        self.overlay.reset_colors_to_default()
        self.jp_swatch.setColor(self.overlay.color_jp)
        self.en_swatch.setColor(self.overlay.color_en)
        self.reset_colors_btn.flash_status("戻しました")

    def _confirm_quit(self) -> bool:
        box = QMessageBox(self)
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle("IME Aura")
        box.setText("IME Aura を終了しますか？")
        box.setInformativeText("画面縁のグラデーション表示も消えます。")
        box.setStandardButtons(
            QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes
        )
        box.setDefaultButton(QMessageBox.StandardButton.Cancel)
        yes = box.button(QMessageBox.StandardButton.Yes)
        cancel = box.button(QMessageBox.StandardButton.Cancel)
        if yes is not None:
            yes.setText("終了")
        if cancel is not None:
            cancel.setText("キャンセル")
        return box.exec() == QMessageBox.StandardButton.Yes

    def _request_quit(self) -> None:
        if self._quitting:
            return
        if not self._confirm_quit():
            return
        self._quitting = True
        QApplication.quit()

    def closeEvent(self, event) -> None:
        if self._quitting:
            event.accept()
            return
        if not self._confirm_quit():
            event.ignore()
            return
        self._quitting = True
        QApplication.quit()
        event.accept()
