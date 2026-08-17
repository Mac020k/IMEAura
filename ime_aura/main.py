"""Application entry point."""

from __future__ import annotations

import logging
import sys

from PySide6.QtWidgets import QApplication, QStyleFactory

from ime_aura.platform import create_backend
from ime_aura.settings import load_settings, ui_font_point_size
from ime_aura.ui import ControlWindow, ImeOverlay
from ime_aura.ui import theme
from ime_aura.ui.icons import load_svg_icon


def main() -> None:
    logging.basicConfig(level=logging.WARNING, format="%(levelname)s: %(message)s")

    backend = create_backend()
    backend.setup_app_identity()

    app = QApplication(sys.argv)
    # Fusion makes QSS scrollbars / button states reliable on Windows.
    if "Fusion" in QStyleFactory.keys():
        app.setStyle("Fusion")
    app.setWindowIcon(load_svg_icon())
    app.setStyleSheet(theme.application_stylesheet())
    settings = load_settings()
    app.setFont(theme.apply_app_font(ui_font_point_size(settings.ui_font_size)))

    overlay = ImeOverlay(backend)
    overlay.show()

    control = ControlWindow(overlay)
    control.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
