"""Helpers for loading SVG icons as pixmaps / QIcon."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon, QPainter, QPixmap
from PySide6.QtSvg import QSvgRenderer

from ime_aura.resources import resource_path


def svg_path() -> str:
    return resource_path("img/icon.svg")


def load_svg_pixmap(size: int = 64) -> QPixmap:
    path = svg_path()
    renderer = QSvgRenderer(path)
    if not renderer.isValid():
        # Fallback for packaged builds that only ship .ico
        pix = QPixmap(resource_path("img/icon.ico"))
        if pix.isNull():
            pix = QPixmap(resource_path("img/icon.png"))
        if pix.isNull():
            return QPixmap()
        return pix.scaled(
            size,
            size,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )

    pix = QPixmap(size, size)
    pix.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pix)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    renderer.render(painter)
    painter.end()
    return pix


def load_svg_icon(size: int = 256) -> QIcon:
    pix = load_svg_pixmap(size)
    if pix.isNull():
        return QIcon(resource_path("img/icon.ico"))
    icon = QIcon()
    icon.addPixmap(pix)
    # Provide a couple of common sizes for the window chrome.
    for s in (16, 32, 48, 64):
        if s != size:
            icon.addPixmap(load_svg_pixmap(s))
    return icon
