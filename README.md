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
│       ├── control_window.py
│       └── about_dialog.py  # バージョン情報・ライセンス表記
├── img/
│   ├── icon.png
│   └── icon.ico
├── requirements.txt
├── LICENSE
├── THIRD_PARTY_NOTICES.md   # 第三者ソフトウェア表記
└── README.md
```

## 特徴

- **リアルタイムな状態検知**: アクティブな入力状態を取得し、日本語入力か英語入力かを判定します。
- **画面縁のグラデーション**: 状態に応じて画面の縁にグラデーションを描画します。
- **マルチディスプレイ対応**: アクティブウィンドウがあるディスプレイに自動で追従します（環境により制限あり）。
- **色のカスタマイズ**: コントロールウィンドウから、日本語入力時・英語入力時の色（透明度含む）を変更できます。色・表示モード・グラデーション幅は次回起動時も保持されます。
- **表示モード**: グラデーションを常時表示するか、テキスト入力時のみ表示するかを選べます。テキスト入力時のみのとき、テキストボックスへのホバーでも表示するかを追加で選べます。
- **入力透過**: オーバーレイはクリック等を透過するため、作業の邪魔になりません。

## 動作環境

| OS | IME 検知 | 画面追従 | テキスト入力検知 |
| --- | --- | --- | --- |
| Windows | IMM32（標準） | アクティブウィンドウ | ウィンドウクラス + MSAA |
| macOS | Carbon Text Input Source | Quartz（失敗時はカーソル位置） | Accessibility（許可が必要な場合あり） |
| Linux | Fcitx5 または IBus（自動検出） | X11 + `xdotool`（Wayland はカーソル位置にフォールバック） | AT-SPI（`python3-gi` + Atspi がある場合） |

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

<p align="center">
  <img src="img/control_window.png" alt="Control Window" width="300">
</p>

- **日本語入力時の色**: カラーピッカーで色と透明度を変更できます。
- **英語入力時の色**: カラーピッカーで色と透明度を変更できます。
- **デフォルトの色に戻す**: 色を初期値に戻します。
- **グラデーションの幅**: スライダーまたは数値入力で縁のグラデーション幅（1〜100 px）を変更できます。
- **デフォルトの幅に戻す**: グラデーション幅を初期値（15 px）に戻します。
- **グラデーション表示**:
  - **常に表示** / **テキスト入力時のみ**（どちらか一方）
  - **テキストボックスへホバー時も表示**: 「テキスト入力時のみ」選択時だけ有効です
- **バージョン情報**: ライセンスと第三者ソフトウェア表記を表示します。
- **アプリケーションを終了**: アプリケーション全体を終了します。

## 実行ファイルの作成

PyInstaller でフォルダ形式（`--onedir`）の配布物を作成できます。LGPL ライブラリ（PySide6 / Qt）の差し替えに配慮し、単一ファイル（`--onefile`）ではなく `--onedir` を使用します。`LICENSE` と `THIRD_PARTY_NOTICES.md` も同梱されます。

1. PyInstaller をインストールします。

```bash
pip install pyinstaller
```

2. プロジェクトのルートでビルドします。

**Windows:**

```bash
pyinstaller --noconsole --onedir --icon=img/icon.ico --add-data "img/icon.ico;img" --add-data "LICENSE;." --add-data "THIRD_PARTY_NOTICES.md;." -n IMEAura ime_aura/__main__.py
```

**macOS / Linux:**

```bash
pyinstaller --noconsole --onedir --icon=img/icon.ico --add-data "img/icon.ico:img" --add-data "LICENSE:." --add-data "THIRD_PARTY_NOTICES.md:." -n IMEAura ime_aura/__main__.py
```

3. 完了後、`dist/IMEAura/` に実行ファイルと依存ライブラリが生成されます。このフォルダ一式を配布してください。

## GitHub リリースの自動作成

`main` ブランチへプッシュするたびに、GitHub Actions が Windows / Linux / macOS 版アプリケーションをビルドし、新しいプレリリースを作成します。リリースは `v1.0.0-build.<GitHub Actions の実行番号>` 形式の一意なタグで作成され、プッシュされたコミットを指します。

各リリースの Assets には、次の zip が添付されます。展開後、フォルダ内の実行ファイルを起動してください。

| Asset | 対象 |
| --- | --- |
| `IMEAura-windows-x64.zip` | Windows (x64) |
| `IMEAura-linux-x64.zip` | Linux (x64) |
| `IMEAura-macos-arm64.zip` | macOS (Apple Silicon) |

## ライセンス

- **IME Aura（本プロジェクト）**: MIT License（`LICENSE` を参照）
- **第三者ソフトウェア**: `THIRD_PARTY_NOTICES.md` を参照

主な依存である PySide6 / Qt は、LGPL-3.0 / GPL-2.0 / GPL-3.0（または Qt 商用ライセンス）のもとで提供されます。アプリの「バージョン情報」からも同じ内容を確認できます。
