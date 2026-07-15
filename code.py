import sys
import os
import ctypes
import ctypes.wintypes
from PySide6.QtWidgets import (
    QApplication, QWidget, QColorDialog, QPushButton, 
    QVBoxLayout, QHBoxLayout, QLabel
)
from PySide6.QtCore import Qt, QTimer, QRect
from PySide6.QtGui import QPainter, QColor, QLinearGradient, QIcon

# --- IME状態取得用の定数と設定 ---
user32 = ctypes.windll.user32
imm32 = ctypes.windll.imm32

WM_IME_CONTROL = 0x0283
IMC_GETOPENSTATUS = 0x0005
IMC_GETCONVERSIONMODE = 0x0001

IME_CMODE_NATIVE = 0x0001

def resource_path(relative_path):
    """実行時にもPyInstaller化後にもリソースファイルのパスを正しく取得する"""
    try:
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    return os.path.join(base_path, relative_path)

def get_active_window_hwnd():
    """現在アクティブなウィンドウのハンドルを取得する"""
    return user32.GetForegroundWindow()

def get_ime_state(hwnd):
    """
    指定されたウィンドウのIME状態を取得する関数。
    戻り値: True (日本語入力/ひらがな等), False (英語入力/半角英数字)
    """
    if not hwnd:
        return False
    
    # アクティブウィンドウのデフォルトIMEウィンドウハンドルを取得
    default_ime_wnd = imm32.ImmGetDefaultIMEWnd(hwnd)
    if not default_ime_wnd:
        return False
    
    # IMEがONかOFFかを取得
    status = user32.SendMessageW(default_ime_wnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0)
    if status == 0:
        return False # IMEがOFFの場合は英語入力
    
    # 変換モードを取得
    mode = user32.SendMessageW(default_ime_wnd, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0)
    
    # NATIVEビットが立っていれば日本語入力状態（ひらがな/カタカナ）
    if mode & IME_CMODE_NATIVE:
        return True
    
    return False

def get_active_screen_geometry(hwnd):
    """
    指定されたウィンドウが存在するディスプレイ（スクリーン）のジオメトリを取得する
    """
    if not hwnd:
        return None
        
    rect = ctypes.wintypes.RECT()
    if user32.GetWindowRect(hwnd, ctypes.byref(rect)):
        # ウィンドウの中心座標を計算
        cx = (rect.left + rect.right) // 2
        cy = (rect.top + rect.bottom) // 2
        
        # 中心座標が含まれるスクリーンを探す
        for screen in QApplication.screens():
            if screen.geometry().contains(cx, cy):
                return screen.geometry()
                
        # 中心座標で見つからない場合は左上の座標で探す
        for screen in QApplication.screens():
            if screen.geometry().contains(rect.left, rect.top):
                return screen.geometry()
                
    return None

# --- オーバーレイウィンドウクラス ---
class ImeOverlay(QWidget):
    def __init__(self):
        super().__init__()
        
        # ウィンドウフラグの設定：最前面、枠なし、入力透過、タスクバーに表示しない(Tool)
        self.setWindowFlags(
            Qt.WindowType.WindowStaysOnTopHint |
            Qt.WindowType.FramelessWindowHint |
            Qt.WindowType.WindowTransparentForInput |
            Qt.WindowType.Tool
        )
        
        # 背景を透過させる属性を設定
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        
        # 色の初期設定
        self.color_jp = QColor(248, 40, 70, 255) # 半透明の赤
        self.color_en = QColor(45, 129, 253, 255) # 半透明の青
        
        # 初期状態の設定
        hwnd = get_active_window_hwnd()
        self.is_japanese = get_ime_state(hwnd)
        
        # 初期スクリーンの設定
        geo = get_active_screen_geometry(hwnd)
        if geo:
            self.setGeometry(geo)
        else:
            self.setGeometry(QApplication.primaryScreen().geometry())
            
        # タイマーの設定：100ミリ秒ごとにIME状態とアクティブウィンドウの位置をチェック
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.check_state)
        self.timer.start(100)
        
    def set_color_jp(self, color):
        self.color_jp = color
        self.update()
        
    def set_color_en(self, color):
        self.color_en = color
        self.update()
        
    def check_state(self):
        """定期的にIME状態とアクティブウィンドウのスクリーンをチェックし、変化があれば更新する"""
        hwnd = get_active_window_hwnd()
        
        # IME状態のチェック
        new_state = get_ime_state(hwnd)
        state_changed = (new_state != self.is_japanese)
        if state_changed:
            self.is_japanese = new_state
            
        # スクリーンのチェック
        target_geo = get_active_screen_geometry(hwnd)
        geo_changed = False
        if target_geo and target_geo != self.geometry():
            self.setGeometry(target_geo)
            geo_changed = True
            
        # 状態かジオメトリが変わったら再描画
        if state_changed or geo_changed:
            self.update()
            
    def paintEvent(self, event):
        """画面の縁にグラデーションを描画する"""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # IME状態に応じて色を決定
        if self.is_japanese:
            base_color = self.color_jp
        else:
            base_color = self.color_en
            
        transparent = QColor(0, 0, 0, 0) # 完全な透明
        
        width = self.width()
        height = self.height()
        thickness = 15 # グラデーションの縁の太さ
        
        # 上辺のグラデーション
        grad_top = QLinearGradient(0, 0, 0, thickness)
        grad_top.setColorAt(0, base_color)
        grad_top.setColorAt(1, transparent)
        painter.fillRect(0, 0, width, thickness, grad_top)
        
        # 下辺のグラデーション
        grad_bottom = QLinearGradient(0, height, 0, height - thickness)
        grad_bottom.setColorAt(0, base_color)
        grad_bottom.setColorAt(1, transparent)
        painter.fillRect(0, height - thickness, width, thickness, grad_bottom)
        
        # 左辺のグラデーション
        grad_left = QLinearGradient(0, 0, thickness, 0)
        grad_left.setColorAt(0, base_color)
        grad_left.setColorAt(1, transparent)
        painter.fillRect(0, 0, thickness, height, grad_left)
        
        # 右辺のグラデーション
        grad_right = QLinearGradient(width, 0, width - thickness, 0)
        grad_right.setColorAt(0, base_color)
        grad_right.setColorAt(1, transparent)
        painter.fillRect(width - thickness, 0, thickness, height, grad_right)

# --- コントロールウィンドウクラス ---
class ControlWindow(QWidget):
    def __init__(self, overlay):
        super().__init__()
        self.overlay = overlay
        self.setWindowTitle("IME State Viewer")
        
        layout = QVBoxLayout()
        
        # 日本語入力時の色設定
        jp_layout = QHBoxLayout()
        jp_label = QLabel("日本語入力時の色:")
        self.jp_btn = QPushButton()
        self.update_btn_color(self.jp_btn, self.overlay.color_jp)
        self.jp_btn.clicked.connect(self.choose_jp_color)
        jp_layout.addWidget(jp_label)
        jp_layout.addWidget(self.jp_btn)
        layout.addLayout(jp_layout)
        
        # 英語入力時の色設定
        en_layout = QHBoxLayout()
        en_label = QLabel("英語入力時の色:")
        self.en_btn = QPushButton()
        self.update_btn_color(self.en_btn, self.overlay.color_en)
        self.en_btn.clicked.connect(self.choose_en_color)
        en_layout.addWidget(en_label)
        en_layout.addWidget(self.en_btn)
        layout.addLayout(en_layout)
        
        # 終了ボタン
        exit_btn = QPushButton("アプリケーションを終了")
        exit_btn.clicked.connect(QApplication.quit)
        layout.addWidget(exit_btn)
        
        self.setLayout(layout)
        
    def update_btn_color(self, btn, color):
        """ボタンの背景色を現在の色に合わせて更新する"""
        btn.setStyleSheet(f"background-color: rgba({color.red()}, {color.green()}, {color.blue()}, {color.alpha()}); border: 1px solid #ccc;")
        
    def choose_jp_color(self):
        """日本語入力時の色を選択するダイアログを表示"""
        color = QColorDialog.getColor(self.overlay.color_jp, self, "日本語入力時の色を選択", QColorDialog.ColorDialogOption.ShowAlphaChannel)
        if color.isValid():
            self.overlay.set_color_jp(color)
            self.update_btn_color(self.jp_btn, color)
            
    def choose_en_color(self):
        """英語入力時の色を選択するダイアログを表示"""
        color = QColorDialog.getColor(self.overlay.color_en, self, "英語入力時の色を選択", QColorDialog.ColorDialogOption.ShowAlphaChannel)
        if color.isValid():
            self.overlay.set_color_en(color)
            self.update_btn_color(self.en_btn, color)
            
    def closeEvent(self, event):
        """ウィンドウが閉じられたときにアプリケーション全体を終了する"""
        QApplication.quit()
        event.accept()

def main():
    # タスクバーで正しいアイコンを表示するための設定 (Windows)
    try:
        myappid = 'imestateviewer.app.1.0'
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(myappid)
    except Exception:
        pass

    # 実行には PySide6 が必要です (pip install PySide6)
    app = QApplication(sys.argv)
    app.setWindowIcon(QIcon(resource_path("IME.ico")))
    
    overlay = ImeOverlay()
    # showFullScreen() の代わりに show() を使い、setGeometry で指定した画面に表示させる
    overlay.show()
    
    control = ControlWindow(overlay)
    control.show()
    
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
