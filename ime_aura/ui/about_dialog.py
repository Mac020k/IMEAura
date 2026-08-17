"""About dialog with license and third-party notices."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QDialog,
    QDialogButtonBox,
    QHBoxLayout,
    QLabel,
    QTextBrowser,
    QVBoxLayout,
)

from ime_aura import __version__
from ime_aura.resources import resource_path
from ime_aura.ui import theme
from ime_aura.ui.icons import load_svg_icon, load_svg_pixmap
from ime_aura.ui.widgets import SectionRule, paint_atmosphere


def _read_text(relative_path: str) -> str:
    path = resource_path(relative_path)
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except OSError:
        return f"({relative_path} を読み込めませんでした)"


class AboutDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("AboutDialog")
        self.setWindowTitle("IME Aura について")
        self.setWindowIcon(load_svg_icon())
        self.setMinimumSize(480, 420)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(theme.MARGIN, theme.MARGIN, theme.MARGIN, theme.MARGIN)
        layout.setSpacing(theme.ROW_GAP)

        header = QHBoxLayout()
        header.setSpacing(theme.SPACE_3)

        icon = load_svg_pixmap(56)
        if not icon.isNull():
            icon_label = QLabel()
            icon_label.setPixmap(icon)
            icon_label.setFixedSize(56, 56)
            header.addWidget(icon_label, 0, Qt.AlignmentFlag.AlignTop)

        text_col = QVBoxLayout()
        text_col.setSpacing(2)

        title = QLabel("IME Aura")
        title.setObjectName("BrandTitle")
        title_font = QFont(title.font())
        title_font.setPointSize(max(title_font.pointSize() + 5, 18))
        title_font.setWeight(QFont.Weight.DemiBold)
        title.setFont(title_font)

        meta = QLabel(
            f"バージョン {__version__}\n"
            "Copyright (c) 2026 Mac020k"
        )
        meta.setObjectName("SecondaryLabel")
        meta.setWordWrap(True)

        blurb = QLabel(
            "本ソフトウェアは MIT License のもとで提供されています。"
            "GUI には PySide6 / Qt (LGPL-3.0 / GPL-2.0 / GPL-3.0) を利用しています。"
        )
        blurb.setWordWrap(True)

        text_col.addWidget(title)
        text_col.addWidget(meta)
        text_col.addSpacing(theme.SPACE_2)
        text_col.addWidget(blurb)
        header.addLayout(text_col, 1)
        layout.addLayout(header)
        layout.addWidget(SectionRule())

        notices = QTextBrowser()
        notices.setOpenExternalLinks(True)
        notices.setPlainText(
            "--- LICENSE ---\n\n"
            + _read_text("LICENSE")
            + "\n\n--- THIRD_PARTY_NOTICES ---\n\n"
            + _read_text("THIRD_PARTY_NOTICES.md")
        )
        layout.addWidget(notices, stretch=1)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        buttons.rejected.connect(self.accept)
        layout.addWidget(buttons)

    def paintEvent(self, event) -> None:
        paint_atmosphere(self)
        super().paintEvent(event)
