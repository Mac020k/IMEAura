"""Control window for color customization and quitting."""

from __future__ import annotations

from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QApplication,
    QColorDialog,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ime_aura.ui.overlay import ImeOverlay


class ControlWindow(QWidget):
    def __init__(self, overlay: ImeOverlay):
        super().__init__()
        self.overlay = overlay
        self.setWindowTitle("IME Aura")

        layout = QVBoxLayout()

        jp_layout = QHBoxLayout()
        jp_label = QLabel("日本語入力時の色:")
        self.jp_btn = QPushButton()
        self.update_btn_color(self.jp_btn, self.overlay.color_jp)
        self.jp_btn.clicked.connect(self.choose_jp_color)
        jp_layout.addWidget(jp_label)
        jp_layout.addWidget(self.jp_btn)
        layout.addLayout(jp_layout)

        en_layout = QHBoxLayout()
        en_label = QLabel("英語入力時の色:")
        self.en_btn = QPushButton()
        self.update_btn_color(self.en_btn, self.overlay.color_en)
        self.en_btn.clicked.connect(self.choose_en_color)
        en_layout.addWidget(en_label)
        en_layout.addWidget(self.en_btn)
        layout.addLayout(en_layout)

        exit_btn = QPushButton("アプリケーションを終了")
        exit_btn.clicked.connect(QApplication.quit)
        layout.addWidget(exit_btn)

        self.setLayout(layout)

    def update_btn_color(self, btn: QPushButton, color: QColor) -> None:
        btn.setStyleSheet(
            f"background-color: rgba({color.red()}, {color.green()}, "
            f"{color.blue()}, {color.alpha()}); border: 1px solid #ccc;"
        )

    def choose_jp_color(self) -> None:
        color = QColorDialog.getColor(
            self.overlay.color_jp,
            self,
            "日本語入力時の色を選択",
            QColorDialog.ColorDialogOption.ShowAlphaChannel,
        )
        if color.isValid():
            self.overlay.set_color_jp(color)
            self.update_btn_color(self.jp_btn, color)

    def choose_en_color(self) -> None:
        color = QColorDialog.getColor(
            self.overlay.color_en,
            self,
            "英語入力時の色を選択",
            QColorDialog.ColorDialogOption.ShowAlphaChannel,
        )
        if color.isValid():
            self.overlay.set_color_en(color)
            self.update_btn_color(self.en_btn, color)

    def closeEvent(self, event) -> None:
        QApplication.quit()
        event.accept()
