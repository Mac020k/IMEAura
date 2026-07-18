"""Application entry point."""

from __future__ import annotations

import logging
import sys

from PySide6.QtGui import QIcon
from PySide6.QtWidgets import QApplication

from ime_aura.platform import create_backend
from ime_aura.resources import resource_path
from ime_aura.ui import ControlWindow, ImeOverlay


def main() -> None:
    logging.basicConfig(level=logging.WARNING, format="%(levelname)s: %(message)s")

    backend = create_backend()
    backend.setup_app_identity()

    app = QApplication(sys.argv)
    app.setWindowIcon(QIcon(resource_path("img/icon.ico")))

    overlay = ImeOverlay(backend)
    overlay.show()

    control = ControlWindow(overlay)
    control.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
