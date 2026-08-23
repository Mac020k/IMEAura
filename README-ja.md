<p align="center">
  <img src="img/icon.ico" alt="IME Aura Icon" width="180">
</p>

# IME Aura

[English](README.md)

**IME Aura** は、ディスプレイの端にさりげないグラデーションを描画し、アクティブな IME / 入力言語 (日本語・中国語・韓国語・Latin) を示す軽量デスクトップユーティリティです。入力モードの取り違えを防ぐのに役立ちます。

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
- 言語ごとの端の色 (最大7スロット; Phase 1: ja / zh-Hans / zh-Hant / ko / en); グラデーション幅 1–100 px
- 表示モード: 常に表示 / 入力中のみ (+ 任意のホバー表示); Aura の有効/無効トグル
- タブ付き設定 (Aura / Firefly / 一般) — UI は日本語・英語・簡体/繁体中国語・韓国語
- 一般タブの言語ピッカーページ; Aura の色行は言語ドロップダウン
- Aura 色の追加 (プラス) / 削除 (ゴミ箱) と言語ピッカーの戻る (シェブロン) は SVG アイコンで示し、OS が対応する場合はローカライズ済みラベル / ツールチップも付与
- Windows カラーピッカー: RGB/HSB 切替、HEX・数値のテキスト入力、既定色プリセット
- 将来の IME 言語候補: [docs/ime-languages.md](docs/ime-languages.md)
- トレイ / メニューバーアイコン — 設定を閉じてもウィンドウは隠れるだけでアプリは継続動作
- **Firefly**: CapsLock を Available / Busy (おやすみモード) に割当 — LED + 通知抑制 (Windows / macOS / Linux/X11)

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
| `aura_colors` | `[{"lang":"ja\|zh-Hans\|zh-Hant\|ko\|en","color":[r,g,b,a]}, …]` (2–7) | ja + en 既定 |
| `color_jp` / `color_en` | `[r,g,b,a]` 0–255 | 互換用に保存; 優先は `aura_colors` |
| `aura_enabled` | bool | `true` |
| `display_mode` | `always` \| `on_focus` | `always` |
| `show_on_hover` | bool (`on_focus` 時のみ) | `false` |
| `ui_font_size` | `small` \| `medium` \| `large` | `medium` |
| `gradient_width` | 1–100 | `15` |
| `language` | `ja` \| `en` \| `zh-Hans` \| `zh-Hant` \| `ko` | `ja` |
| `firefly_enabled` | bool | `false` |
| `firefly_caps_mode` | `preserve` \| `uppercase` \| `lowercase` | `uppercase` |
| `firefly_led_mode` | `auto` \| `hid` \| `none` | `auto` |

色スロットを ja/en の次に追加すると、既定色は順に `#16CC7B`、`#F1D60F`、`#E6690C`、`#7E43D5`、`#636363` です。

## Firefly (Windows / macOS / Linux/X11)

Firefly は、IME Aura 動作中に物理 **CapsLock** キーを Available / Busy (おやすみモード) トグルに割り当てます。

| 状態 | 意味 | CapsLock LED (LED 制御オン時) | 通知 |
| --- | --- | --- | --- |
| **Available** | 有効化直後の既定 | 消灯 | 通常 |
| **Busy** | CapsLock 押下後 | 点灯 | 抑制 |

- Firefly を有効にすると、常に **Available** から開始します。
- CapsLock を押すたびに Available ↔ Busy を切り替えます。
- Firefly 有効中、CapsLock はシステムの大文字/小文字切替をしません。大文字小文字は `firefly_caps_mode` に従います。
- Firefly を無効にすると、可能な範囲で以前の CapsLock と DND 設定を復元します。
- フェイルクローズ: プラットフォームのインターセプトをインストールできない場合、有効化は拒否されます (`firefly_enabled` をクリア)。

| プラットフォーム | インターセプト | LED | DND |
| --- | --- | --- | --- |
| **Windows** | `WH_KEYBOARD_LL` | CapsLock ビット / HID | レジストリ Quiet Hours |
| **macOS** | `CGEventTap` (アクセシビリティ許可が必要) | CapsLock 状態 | `defaults` Notification Center UI |
| **Linux** | X11 `XGrabKey` + XTest | sysfs または XKB インジケータ | GNOME `gsettings` (書き込み可能時) |

詳細は [docs/firefly.md](docs/firefly.md) を参照。macOS / Linux では設定 UI 完成前は `settings.json` で Firefly を設定します。

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
| `auto` | Busy ランプとしてネイティブ CapsLock LED を優先; Linux では sysfs `*capslock` も試行 |
| `hid` | HID/sysfs のみで LED を駆動 (Windows HID レポート; Linux sysfs) |
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
