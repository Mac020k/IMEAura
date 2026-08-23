<p align="center">
  <img src="img/icon.ico" alt="IME Aura Icon" width="180">
</p>

# IME Aura

[English](README.md)

**IME Aura** は、ディスプレイの端にさりげないグラデーションを描画し、IME 入力が日本語か英語かを示す軽量デスクトップユーティリティです。入力モードの取り違えを防ぐのに役立ちます。

アプリはネイティブの **C++20** プロジェクトです。オーバーレイと設定 UI は各 OS のネイティブ API を使用します。

| プラットフォーム | オーバーレイ | 設定 UI |
| --- | --- | --- |
| **Windows** 10 1803+ | Windows.UI.Composition | Win32 + Direct2D / DirectWrite |
| **macOS** 11+ | 端の `NSWindow` + `CAGradientLayer` | AppKit |
| **Linux** | Wayland `wlr-layer-shell` (X11 フォールバック) | GTK 4 |

## 機能

- プラットフォーム API によるリアルタイム IME 状態検知
- クリック透過の端オーバーレイ (常に最前面)
- マルチモニタ追従 (アクティブウィンドウ; ホバー時のみの場合はカーソル)
- JP / EN のカスタム色 (アルファ付き); グラデーション幅 1–100 px
- 表示モード: 常に表示 / 入力中のみ / 非表示 (+ 任意のホバー表示)
- タブ付き設定 (Aura / Firefly / 一般) — UI は日本語・英語対応
- トレイ / メニューバーアイコン — 設定を閉じてもウィンドウは隠れるだけでアプリは継続動作
- **Firefly** (Windows): CapsLock を Available / Busy (おやすみモード) に割当 — LED + Quiet Hours

## インストール

### ビルド済みバイナリ (推奨)

1. [Releases](https://github.com/Mac020k/IMEAura/releases) ページを開く。
2. OS 用の zip をダウンロードする:
   - `IMEAura-windows-x64.zip`
   - `IMEAura-macos-arm64.zip`
   - `IMEAura-linux-x64.zip`
3. 展開して実行する:
   - **Windows:** `IMEAura.exe` (静的 CRT — Visual C++ 再頒布可能パッケージ不要)
   - **macOS:** `IMEAura.app`
   - **Linux:** `./IMEAura`

`main` への push で GitHub Actions 経由の成果物が公開されます。

### ソースからビルド

#### 要件

| ツール / OS | 備考 |
| --- | --- |
| **CMake** | 3.25+ |
| **Windows** | Visual Studio 2026 (または 2022) Build Tools に *Desktop development with C++*; Windows SDK 10.0.22621+ |
| **macOS** | Xcode Command Line Tools、Ninja |
| **Linux** | GCC または Clang、Ninja、`pkg-config`、および: `libwayland-dev`、`wayland-protocols`、`libdbus-1-dev`、`libatspi2.0-dev`、`libgtk-4-dev` |

#### 設定・ビルド・テスト

```bash
# プリセットを選択: windows-msvc | macos-clang | linux-ninja
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --test-dir build/windows-msvc -C Release --output-on-failure
```

| プリセット | 出力バイナリ |
| --- | --- |
| `windows-msvc` | `build/windows-msvc/src/Release/IMEAura.exe` |
| `macos-clang` | `build/macos-clang/src/IMEAura` |
| `linux-ninja` | `build/linux-ninja/src/IMEAura` |

#### VS Code / Cursor

1. `.vscode/extensions.json` の拡張機能をインストールする (clangd + CMake Tools)。Microsoft C/C++ IntelliSense の無効化を求められたら無効化する。
2. **CMake: Configure** → プリセット `windows-msvc` (または使用プラットフォームのプリセット)。
3. **CMake: build (windows-msvc Release)** でビルドするか、**IMEAura (Windows)** で F5 を押す。

利用可能な場合、`compile_commands.json` は clangd 用にリポジトリルートへコピーされます。

## 実行

```bash
# Windows
./build/windows-msvc/src/Release/IMEAura.exe

# macOS / Linux
./build/macos-clang/src/IMEAura
./build/linux-ninja/src/IMEAura
```

- 初回起動時に設定が開きます。ウィンドウを閉じると隠れますが、アプリはトレイ / メニューバーに残ります。
- 終了はトレイ → **終了** / **Quit** (確認ダイアログあり)。

### プローブモード (診断用、オーバーレイなし)

```bash
IMEAura.exe --probe --json
```

## 設定

JSON として保存されます (`src/core/settings.{h,cpp}`):

| OS | パス |
| --- | --- |
| Windows | `%APPDATA%/IMEAura/settings.json` |
| macOS | `~/Library/Application Support/IMEAura/settings.json` |
| Linux | `$XDG_CONFIG_HOME/ime_aura/settings.json` (既定は `~/.config/ime_aura/`) |

| キー | 値 | 既定 |
| --- | --- | --- |
| `color_jp` / `color_en` | `[r,g,b,a]` 0–255 | JP 赤系 / EN 青 |
| `display_mode` | `always` \| `on_focus` \| `hidden` | `always` |
| `show_on_hover` | bool (`on_focus` 時のみ) | `false` |
| `ui_font_size` | `small` \| `medium` \| `large` | `medium` |
| `gradient_width` | 1–100 | `15` |
| `language` | `ja` \| `en` | `ja` |
| `firefly_enabled` | bool | `false` |
| `firefly_caps_mode` | `preserve` \| `uppercase` \| `lowercase` | `uppercase` |
| `firefly_led_mode` | `auto` \| `hid` \| `none` | `auto` |

## Firefly (Windows)

Firefly は、IME Aura 動作中に物理 **CapsLock** キーを Available / Busy (おやすみモード) トグルに割り当てます。

| 状態 | 意味 | CapsLock LED (LED 制御オン時) | 通知 |
| --- | --- | --- | --- |
| **Available** | 有効化直後の既定 | 消灯 | 通常 |
| **Busy** | CapsLock 押下後 | 点灯 | 抑制 (集中モード / Quiet Hours) |

- Firefly を有効にすると、常に **Available** から開始します。
- CapsLock を押すたびに Available ↔ Busy を切り替えます。
- Firefly 有効中、CapsLock はシステムの大文字/小文字切替をしません。大文字小文字は `firefly_caps_mode` に従います。
- Firefly を無効にすると、可能な範囲で以前の CapsLock と Quiet Hours の設定を復元します。
- フェイルクローズ: 低レベルキーボードフックをインストールできない場合、有効化は拒否され UI トグルはオフのままです。

他プラットフォームに Firefly バックエンドはありません (`create_firefly_backend()` は `nullptr` を返します)。

### CapsLock 状態 (`firefly_caps_mode`)

| 値 | 効果 |
| --- | --- |
| `preserve` | 有効化直前の CapsLock 大文字/小文字極性を維持 |
| `uppercase` | 文字は既定で大文字; Shift で反転 |
| `lowercase` | 文字は既定で小文字; Shift で反転 |

日本語 IME の変換入力はそのまま通します。Ctrl / Alt / Win ショートカットはリマップしません。

### LED モード (`firefly_led_mode`)

| 値 | 効果 |
| --- | --- |
| `auto` | Busy ランプとしてネイティブ CapsLock LED を優先; フォールバックは HID |
| `hid` | HID 出力レポートのみで LED を駆動 |
| `none` | Busy / Available 用に CapsLock LED を駆動しない |

Windows では CapsLock LED と文字の大文字小文字は同一のトグルビットを共有します。Firefly の既定パスはそのビットを Busy ランプに使い、ラテン A–Z を書き換えて `firefly_caps_mode` XOR Shift に従う大文字小文字にします。

## プロジェクト構成

```
src/app/            エントリポイントとアプリ配線
src/core/           設定、ポリシー、i18n、Firefly 状態機械
src/platform/       windows | macos | linux バックエンド
tests/              ユニットテスト (settings, policy, i18n, Firefly, layout)
docs/parity.md      機能チェックリスト
docs/bench.md       メモリ / CPU 測定メモ
```

## ライセンス

MIT License — [LICENSE](LICENSE) を参照。サードパーティ表記: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
