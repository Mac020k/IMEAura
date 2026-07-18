<p align="center">
  <img src="img/icon.ico" alt="IME Aura Icon" width="180">
</p>

# IME Aura

IME Aura は、現在の IME 入力状態（日本語入力か英語入力か）に応じて、画面の縁にグラデーションを表示するオーバーレイツールです。Windows / macOS / Linux で動作します。入力状態を視覚的に把握しやすくすることで、入力ミスを防げます。

## プロジェクト構成

```
IMEAura/
├── ime_aura/
│   ├── __main__.py          # python -m ime_aura
│   ├── main.py              # アプリケーション起動
│   ├── resources.py         # リソースパス解決
│   ├── platform/            # OS 別 IME・画面検知
│   │   ├── base.py
│   │   ├── windows.py
│   │   ├── macos.py
│   │   └── linux.py
│   └── ui/
│       ├── overlay.py       # 縁グラデーション表示
│       └── control_window.py
├── img/
│   ├── icon.png
│   └── icon.ico
├── requirements.txt
├── LICENSE
└── README.md
```

## 特徴

- **リアルタイムな状態検知**: アクティブな入力状態を取得し、日本語入力か英語入力かを判定します。
- **画面縁のグラデーション**: 状態に応じて画面の縁にグラデーションを描画します。
- **マルチディスプレイ対応**: アクティブウィンドウがあるディスプレイに自動で追従します（環境により制限あり）。
- **色のカスタマイズ**: コントロールウィンドウから、日本語入力時・英語入力時の色（透明度含む）を変更できます。
- **入力透過**: オーバーレイはクリック等を透過するため、作業の邪魔になりません。

## 動作環境

| OS | IME 検知 | 画面追従 |
| --- | --- | --- |
| Windows | IMM32（標準） | アクティブウィンドウ |
| macOS | Carbon Text Input Source | Quartz（失敗時はカーソル位置） |
| Linux | Fcitx5 または IBus（自動検出） | X11 + `xdotool`（Wayland はカーソル位置にフォールバック） |

- Python 3.10+
- PySide6
- Linux では Fcitx5（`fcitx5-remote`）または IBus が必要です。どちらも無い場合は常に英語入力として表示されます。

## インストール

1. Python がインストールされていることを確認します。
2. 依存ライブラリをインストールします。

```bash
pip install -r requirements.txt
```

## 使い方

プロジェクトのルートディレクトリで次を実行します。

```bash
python -m ime_aura
```

起動すると、画面の縁にグラデーションが表示され、小さなコントロールウィンドウも開きます。

### コントロールウィンドウ

- **日本語入力時の色**: カラーピッカーで色と透明度を変更できます。
- **英語入力時の色**: カラーピッカーで色と透明度を変更できます。
- **アプリケーションを終了**: アプリケーション全体を終了します。

## 実行ファイルの作成

PyInstaller で単一実行ファイルにできます。

1. PyInstaller をインストールします。

```bash
pip install pyinstaller
```

2. プロジェクトのルートでビルドします。

**Windows:**

```bash
pyinstaller --noconsole --onefile --icon=img/icon.ico --add-data "img/icon.ico;img" -n IMEAura ime_aura/__main__.py
```

**macOS / Linux:**

```bash
pyinstaller --noconsole --onefile --icon=img/icon.ico --add-data "img/icon.ico:img" -n IMEAura ime_aura/__main__.py
```

3. 完了後、`dist` フォルダに実行ファイルが生成されます。

## ライセンス

このプロジェクトのライセンスは `LICENSE` を参照してください。
