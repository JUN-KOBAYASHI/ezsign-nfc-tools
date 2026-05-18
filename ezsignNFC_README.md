# ezsignNFC — NFC 4色 e-Paper(EZ Sign) を Arduino から書き換える

`ezsignNFC` は NFC で書き換え可能な 4 色 e-Paper (EZ Sign) (AID
`D2 76 00 00 85 01 01`) に画像を書き込むための Arduino ライブラリです。

**対応サイズ**:
- 4.2-inch / 400 × 300
- 2.9-inch / 296 × 128

ESP32 (M5AtomS3 など) + PN5180 NFC リーダーを想定。
[改訂版プロトコル仕様書](extras/PROTOCOL.md) も同梱しています。

---

## 主な機能

- ISO 14443-A の活性化、RATS (ISO 14443-4 昇格)、APDU 送受信
- 認証 / デバイス情報取得 / 画像送信 / 画面更新コマンド
- 自動 LZO 圧縮 + 1 ブロック = 1 フラグメント送信 (実機の `SW=6992` 不具合を回避)
- 画像 RGB 入力対応 (内蔵パレットで 4 色量子化)
- ノンブロッキング更新 + 自動 NFC 再接続 (画面更新中の接続切断対策)
- 4.2-inch 4-color の左右反転対応 (`flipH = true`)

---

## クイックスタート

### 必要なもの

- M5AtomS3 (または互換の ESP32-S3 ボード)
- PN5180 NFC リーダーモジュール
- NFC 書き換え可能な 4 色 e-Paper '(EZ Sign) (4.2-inch または 2.9-inch)
- ジャンパーワイヤー × 9 本
- USB-C ケーブル

### Step 1. 配線

| PN5180 のピン | M5AtomS3 のピン | 備考 |
| --- | --- | --- |
| `+5V`   | `+5V` | 電源 |
| `+3.3V` | `3.3V` | **重要**: ほとんどの PN5180 モジュールは両方の電源が必要 |
| `GND`   | `GND` | |
| `RST`   | `G38` | リセット |
| `NSS`   | `G5`  | SPI チップセレクト |
| `MOSI`  | `G8`  | SPI |
| `MISO`  | `G6`  | SPI |
| `SCK`   | `G7`  | SPI |
| `BUSY`  | `G2`  | ビジーフラグ |
| `IRQ`   | (未接続) | このライブラリでは使いません |

> **注**: ピンを変更したい場合は後述の「ピンのカスタマイズ」を参照。

### Step 2. Arduino IDE のセットアップ

1. **Arduino IDE 2.x** をインストール (Arduino 公式サイトから)
2. **ESP32 ボードマネージャを追加**:
   - `ファイル` → `基本設定` の「追加のボードマネージャの URL」に
     `https://espressif.github.io/arduino-esp32/package_esp32_index.json` を追加
   - `ツール` → `ボード` → `ボードマネージャ` で「esp32」を検索してインストール
3. **ボードを選択**: `ツール` → `ボード` → `ESP32 Arduino` → `M5Stack-AtomS3`

### Step 3. ライブラリをインストール

#### 3-A. Library Manager から入るもの

`ツール` → `ライブラリを管理…` で:
- 「**M5Unified**」を検索 → インストール

#### 3-B. PN5180 (GitHub から手動取得)

ATrappmann 製の PN5180 ライブラリは **Arduino Library Manager には登録されていません**。
GitHub から取得します:

1. [https://github.com/ATrappmann/PN5180-Library](https://github.com/ATrappmann/PN5180-Library) を開く
2. 緑の「**Code**」ボタン → 「**Download ZIP**」をクリック
3. Arduino IDE で:
   `スケッチ` → `ライブラリをインクルード` → **`.ZIP 形式のライブラリをインストール…`** を選択
4. ダウンロードした `PN5180-Library-master.zip` を選択

> または、`<Arduino>/libraries/` に zip を展開して `PN5180-Library` フォルダを
> 配置する方法でも構いません。

#### 3-C. ezsignNFC (このライブラリ)

このフォルダ (またはダウンロードした zip) を、
- **zip ファイル経由**: `スケッチ` → `ライブラリをインクルード` → `.ZIP 形式のライブラリをインストール…` から zip を選択
- **手動**: `<Arduino>/libraries/ezsignNFC/` に配置

`<Arduino>` の場所:
- macOS / Linux: `~/Documents/Arduino` または `~/Arduino`
- Windows: `C:\Users\<ユーザー名>\Documents\Arduino`

#### 3-D. miniLZO (各スケッチフォルダに手動配置)

miniLZO は Arduino Library Manager に登録されていないので、手動で配置します。
このライブラリでは、**各スケッチの `.ino` ファイルと同じフォルダ**に 4 ファイル
(`minilzo.c`, `minilzo.h`, `lzoconf.h`, `lzodefs.h`) を置く方式を採用しています。

> なぜスケッチフォルダ？ Arduino IDE の Library Manager に登録されていない
> ライブラリの代替手段として、**スケッチに同梱**するのが最もシンプルだからです。
> このライブラリ (ezsignNFC) はヘッダオンリー構成 (`.cpp` を持たない) で
> 実装されているので、ユーザースケッチのコンパイル時に miniLZO が同じスコープに
> 居ればうまく動きます。

#### 配置手順

1. [https://github.com/yuhaoth/minilzo](https://github.com/yuhaoth/minilzo) を開く
2. 緑の「**Code**」 → 「**Download ZIP**」をクリック
3. ダウンロードした `minilzo-master.zip` を展開
4. 以下の 4 ファイルを、**使うスケッチの `.ino` と同じフォルダ**にコピー:
   - `minilzo.c`
   - `minilzo.h`
   - `lzoconf.h`
   - `lzodefs.h`

例えば、`HelloEzsign` を試す場合は:

```
<Arduino>/libraries/ezsignNFC/examples/HelloEzsign/
├── HelloEzsign.ino
├── minilzo.c       ← ここに追加
├── minilzo.h       ← ここに追加
├── lzoconf.h       ← ここに追加
└── lzodefs.h       ← ここに追加
```

> **複数のサンプルを試したい場合は、それぞれのフォルダにコピーが必要**です。
> 一度配置すれば、そのスケッチの再コンパイルでは追加作業は不要です。

### Step 4. スケッチを書き込む

1. `ファイル` → `スケッチ例` → `ezsignNFC` → **`HelloEzsign`** を開く
2. **重要**: スケッチ例として開いた `HelloEzsign` フォルダは
   `<Arduino>/libraries/ezsignNFC/examples/HelloEzsign/` にあります。
   Step 3-D で取得した **miniLZO の 4 ファイルをここに必ずコピー** してください。
   コピー後のフォルダ構成例:
   ```
   ezsignNFC/examples/HelloEzsign/
   ├── HelloEzsign.ino
   ├── minilzo.c
   ├── minilzo.h
   ├── lzoconf.h
   └── lzodefs.h
   ```
3. ファイルを追加したら、Arduino IDE で `HelloEzsign.ino` を一度閉じて開き直すと、
   タブに `minilzo.c` などが現れます。
4. M5AtomS3 を USB で接続し、`ツール` → `ポート` で適切な COM/tty を選択
5. アップロードボタンを押す
6. シリアルモニタを開く (ボーレート 115200)

> **注**: もし `minilzo.h: No such file or directory` エラーが出たら、
> ファイルが正しくスケッチフォルダにコピーされていません。Step 3-D を確認。

### Step 5. 動かしてみる

1. AtomS3 の画面に「Ready / Place card, press button」と表示される
2. e-Paper 名札を PN5180 のアンテナに密着させる
3. AtomS3 の前面ボタン(BtnA)を押す
4. シリアルに動作ログが流れる
5. 30〜60 秒後、**e-Paper が真っ白**に塗り替わる
6. もう一度押すと **黒** → **黄色** → **赤** と循環

すべてうまく動けば、配線・ライブラリ・カードのすべてが正しく機能しています。

---

## ピンのカスタマイズ

デフォルトピンは M5AtomS3 用です。別のボードや別の配線にしたい場合は、
`EzsignPins` 構造体を渡します:

```cpp
EzsignPins pins;
pins.rst  = 9;
pins.nss  = 10;
pins.mosi = 11;
pins.miso = 12;
pins.sck  = 13;
pins.busy = 14;
EzsignDevice ez(pins);   // この EzsignDevice を以後使う
```

部分的な変更も可能 (構造体のフィールド単位):

```cpp
EzsignPins pins;            // すべて M5AtomS3 デフォルトで埋まる
pins.nss = 21;              // CS だけ変更
pins.busy = 22;             // BUSY だけ変更
EzsignDevice ez(pins);
```

> 注: ピンを変更しても **電源 (+5V と +3.3V)** は両方供給が必要なことは
> 変わりません。

### 上級者: 自前の PN5180ISO14443 インスタンスを使う

既に PN5180 を SPI で初期化してある場合は、そのインスタンスを渡せます:

```cpp
PN5180ISO14443 nfc(NSS_PIN, BUSY_PIN, RST_PIN);
EzsignDevice ez(nfc);   // この場合、SPI.begin() は自分でやる
```

---

## 最小コード例

```cpp
#include <M5Unified.h>
#include <ezsignNFC.h>

EzsignDevice ez;   // デフォルトピン (M5AtomS3)

void setup() {
  M5.begin();
  Serial.begin(115200);
  ez.setLogger([](const char* m) { Serial.println(m); });
  ez.begin();      // SPI.begin() もここでやってくれる
}

void writeImage() {
  // すべて白の画像を作る (パレットインデックス: 0=黒, 1=白, 2=黄, 3=赤)
  static uint8_t img[400 * 300];
  memset(img, COLOR_WHITE, sizeof(img));
  // 黒い枠線を描く
  for (int y = 0; y < 300; ++y) {
    for (int x = 0; x < 400; ++x) {
      if (x < 8 || x >= 392 || y < 8 || y >= 292) {
        img[y * 400 + x] = COLOR_BLACK;
      }
    }
  }

  if (!ez.detect()) return;
  if (!ez.authenticate()) return;
  if (!ez.sendImageIndices(img, 400, 300, /*flipH=*/true)) return;
  if (!ez.refreshDisplay(/*blocking=*/false)) return;
  ez.waitForRefresh();          // ~30-60 s
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) writeImage();
  delay(20);
}
```

サイズを自動取得して書き込みたい場合は `examples/AutoSize/` を参照してください。

---

## API

### コンストラクタ

```cpp
EzsignDevice();                          // デフォルトピン (M5AtomS3)
EzsignDevice(const EzsignPins& pins);    // ピンを上書き
EzsignDevice(PN5180ISO14443& nfc);       // 既存の PN5180 インスタンスを使う
```

最初の 2 つは内部で `PN5180ISO14443` を生成し、`begin()` で `SPI.begin()`
もやってくれます。3 つ目は上級者向けで、`SPI.begin()` は自分で呼ぶ必要があります。

### `bool begin()`
ヒープバッファ (LZO ワーク 64 KB 含む) を確保し、PN5180 を初期化します。
**`SPI.begin()` の後**、`setup()` 内で呼んでください。

### `bool detect()`
カードを検出し ISO 14443-4 セッションを開始 (REQA → SELECT → RATS)。

### `bool authenticate()`
認証 APDU (`00 20 00 01 04 20 09 12 10`) を送信。

### `bool getDeviceInfo(EzsignDeviceInfo& out)`
デバイス情報 (サイズ、色深度、UID、名前) を取得。

### `bool sendImageIndices(const uint8_t* indices, int width, int height, bool flipH)`
**色インデックス配列**から画像を送信。各バイト = `0`(黒)/`1`(白)/`2`(黄)/`3`(赤)。

### `bool sendImageRGB(const uint8_t* rgb, int width, int height, bool flipH)`
**RGB 配列**から画像を送信。内部で最近傍量子化 (ディザリングなし)。

### `bool refreshDisplay(bool blocking)`
画面更新コマンドを送信。

| `blocking` | 動作 |
| --- | --- |
| `false` (推奨) | 即時応答。`waitForRefresh()` で完了を待つ |
| `true` | 応答が更新完了まで返らない (30〜60 秒) |

### `bool waitForRefresh(uint32_t intervalMs = 500, uint32_t maxSeconds = 90)`
更新完了をポーリング。途中で接続が切れても自動的に再接続を試みます。

### `void setLogger(EzsignLogger l)`
ログコールバックを設定。`nullptr` で無効化。

```cpp
ez.setLogger([](const char* m) { Serial.println(m); });
```

### `void setOversizedBlockBehavior(EzsignOversizedBehavior b)`

| 値 | 動作 |
| --- | --- |
| `EZSIGN_BLOCK_SUBSTITUTE_WHITE` (デフォルト) | 圧縮しきれないブロックを白で代替 |
| `EZSIGN_BLOCK_FAIL` | エラーで停止 |

### `EzsignError lastError()` / `errorString(e)`
最近のエラーコードと、その文字列表現。

---

## 重要な仕様上の知見

知っておくと役に立つ事項:

### ISO 14443-4 I-block チェイニングを自動で扱います

仕様書通り **250 バイトのフラグメント**を使うと、APDU 全長 は 257 バイト
(5 + 2 + 250) になり、**ISO 14443-4 の単一フレーム制限 (FSC=256) を
超えます**。`ezsignNFC` は内部で **I-block チェイニング** を実装しており、
長い APDU を複数フレームに分割して自動的に送信します。

詳細: [PROTOCOL.md §5.1](extras/PROTOCOL.md#51-長い-apdu-と-iso-dep-i-block-チェイニング)

### 圧縮しやすい画像のほうが速い

**送信時間は圧縮後サイズに比例**します:

- ベタ白ブロック (36 B) → 1 フラグメント → ~30 ms
- 中程度の絵柄 (500 B) → 2 フラグメント → ~60 ms
- 写真風 (1500 B) → 6 フラグメント → ~180 ms

書き換え速度を稼ぎたい場合は、白基調・ベタ塗り多めの設計が有効です。

### 4.2-inch 4-color は flipH = true が安全

公式仕様では「そのまま」となっていますが、ファームウェア個体差があります。
`flipH = false` で鏡像表示されたら `true` にしてください。

### 2.9-inch は 90 度回転が必要

`getDeviceInfo()` で width/height が `(128, 296)` または `(296, 128)` として
返ります (個体差)。論理座標 296×128 (横長) → 送信座標 128×296 (縦長) への
回転変換は呼び出し側で行います。詳細:
[PROTOCOL.md §5.8](extras/PROTOCOL.md#58-29-inch--42-inch-のサイズ別注意点)

---

## トラブルシューティング

| 症状 | 対処 |
| --- | --- |
| `minilzo.h: No such file or directory` | スケッチフォルダに `minilzo.h` 等の 4 ファイルを配置していない (Step 3-D) |
| `undefined reference to lzo1x_1_compress` | `minilzo.c` のみコピー忘れ。`.c` も含む 4 ファイルすべて必要 |
| `begin failed: out of memory` | LZO ワークに 64 KB 必要。ESP32-S3 系を使う |
| シリアルログは出るが、カードを検出しない | PN5180 に `+3.3V` も配線しているか確認 (重要) |
| `detect: no card` | カードを PN5180 アンテナに密着させる。距離 1〜2 mm 以内 |
| `auth: ...` で失敗 | 別仕様のカードの可能性。`getDeviceInfo()` の応答を確認 |
| 画像が**鏡像**で表示 | `sendImageIndices()` の `flipH` パラメータを反転 (true ⇔ false) |
| 画像が**ずれて**表示 | カードが対応する以外のサイズで画像を送っている。`getDeviceInfo()` の width/height を使う |
| 2.9-inch で**横向きに伸びた**画像が表示 | 90 度回転が必要。`AutoSize.ino` を参考に回転ロジックを実装 |
| `Refreshing...` のままタイムアウト | 仕様。`waitForRefresh()` は失敗してもイメージは更新されている可能性が高い |
| シリアルがロックする | M5AtomS3 が deep sleep に入った可能性。リセットボタンを押す |

詳細なプロトコル仕様と原因分析は
[改訂版プロトコル仕様書](extras/PROTOCOL.md) を参照。

---

## さらに詳しく

- [改訂版プロトコル仕様書](extras/PROTOCOL.md) — 元の niw 氏のリバースエンジニアリング +
  実機検証で発見した仕様外の制約・対処法を網羅。**§9 に SONY RC-S300 (PaSoRi 4.0) を
  WebUSB から扱う実装仕様**を記載。
- `examples/HelloEzsign/` — 最小サンプル (固定 400×300)
- `examples/SimpleImage/` — RGB 画像入力サンプル
- `examples/AutoSize/` — `getDeviceInfo()` で**サイズ自動検出** (2.9/4.2-inch 両対応)
- `ezsign-rcs300-writer.html` — **ブラウザ単体で動く RC-S300 + WebUSB 実装**
  (Chrome / Edge で `file://` から開くと、画像読み込み → 4色変換 → ezsign へアップロードまで完結)

---

## ライセンス

- `ezsignNFC` 本体: MIT
- 依存: `miniLZO` は GNU GPL v2 以上, `PN5180-Library` は LGPL v2.1 以上。
  これらは別途配布される必要があります。
