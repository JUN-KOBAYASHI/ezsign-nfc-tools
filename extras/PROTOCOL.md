# NFC e-Paper (EZ Sign) プロトコル仕様（実装ガイド付き）

NFC で書き換え可能な 4 色 e-Paper 名札のプロトコル仕様書です。

[niw 氏のリバースエンジニアリング](https://gist.github.com/niw/3885b22d502bb1e145984d41568f202d)
および [@alt-core 氏の追加調査](https://github.com/alt-core/nfc-eink) を元に、
実機 (4.2-inch 4-color, 400×300, AID `D2 76 00 00 85 01 01`) で
動作を検証し、各種参考情報だけでは不足だった点を加筆しています。

---

## 目次

1. [対応デバイス・通信方式](#1-対応デバイス通信方式)
2. [APDU コマンド](#2-apdu-コマンド)
3. [画像データの構造](#3-画像データの構造)
4. [送信手順](#4-送信手順)
5. [実機で発見した重要な制約・修正](#5-実機で発見した重要な制約修正)
6. [PN5180 利用時の注意点](#6-pn5180-利用時の注意点)
7. [ESP32 / M5Stack での実装ガイド](#7-esp32--m5stack-での実装ガイド)
8. [トラブルシュートチェックリスト](#8-トラブルシュートチェックリスト)

---

## 1. 対応デバイス・通信方式

- **物理レイヤー**: ISO 14443 Type-A (13.56 MHz)
- **アプリケーション層**: ISO 7816-4 APDU
- **AID**: `D2 76 00 00 85 01 01`

| サイズ | 色数 (bpp) | 解像度 | 色インデックス | 画像方向 |
| --- | --- | --- | --- | --- |
| 2.9-inch | 2 (1bpp) | 296 × 128 | 0=黒 / 1=白 | 90度回転 |
| 2.9-inch | 4 (2bpp) | 296 × 128 | 0=黒 / 1=白 / 2=黄 / 3=赤 | 90度回転 |
| 4.2-inch | 2 (1bpp) | 400 × 300 | 0=黒 / 1=白 | 左右反転 |
| 4.2-inch | 4 (2bpp) | 400 × 300 | 0=黒 / 1=白 / 2=黄 / 3=赤 | **そのまま (公式仕様)** |

> **実機検証で発見**: 4.2-inch 4-color デバイスでも
> **`flipH = true` (左右反転) が必要**だった個体があります。
> 公式仕様の「そのまま」と矛盾しますが、ファームウェアの個体差の
> 可能性。最初に試して、画像が鏡像表示されたら反転を入れてください。

---

## 2. APDU コマンド

すべて成功時 `SW1SW2 = 9000`。失敗時はそれ以外。

### 2.1 認証 (AUTH)

```
送信: 00 20 00 01 04 20 09 12 10
```

最初に必ず送信。これ以降のコマンドが受け付けられるようになります。

### 2.2 デバイス情報取得 (GET INFO)

```
送信:   00 D1 00 00 00
受信:   TLV (タグ1byte + 長さ1byte + データ) の繰り返し + 9000
```

**主要タグ**:

| タグ | 長さ | 意味 |
| --- | --- | --- |
| `A0` | 7 | 物理パラメータ |
| `A1` | 7 | 画像方向 |
| `B1` `B2` `B3` | 各1 | 用途不明（未確定） |
| `C0` | 10 | デバイス名 (ASCII) |
| `C1` | 4 | UID (Big-Endian) |
| `D1` | 7 | 用途不明 |

**A0 タグ詳細** (7 バイト):
```
unknown(1) | numColors(1) | unknown(1) | heightInBits(2 BE) | width(2 BE)
```
- `numColors`: `0x01` または `0x47` = 2-color (1 bpp), `0x07` = 4-color (2 bpp)
- 真の画像高 = `heightInBits / bpp`

**A1 タグ詳細** (7 バイト):
```
imageOrientation(1) | unknown(6)
```
- `imageOrientation`: `0x00` = 90度回転、`0x01` = そのまま (推定、要検証)

### 2.3 画像データ送信 (IMAGE FRAGMENT)

```
送信: F0 D3 00 P2 Lc DATA
       │  │  │  │  │  │
       │  │  │  │  │  └ ブロック番号(1) + フラグメント番号(1) + フラグメント本体
       │  │  │  │  └ Lc = フラグメント長 + 2
       │  │  │  └ P2: 0x00 = 中間フラグメント、0x01 = ブロックの最終フラグメント
       │  │  └ P1: 0x00 (固定)
       │  └ INS = D3
       └ CLA = F0
```

### 2.4 画面更新 (REFRESH)

```
送信: F0 D4 85 P2 00
                │
                └ P2: 0x00 = ブロッキング (応答が更新完了まで返らない)
                       0x80 = ノンブロッキング (即時応答、ステータスを別途ポーリング)
```

> **ブロッキングモードは 30 〜 60 秒の応答待機**になります。
> iOS Core NFC や PN5180 の標準タイムアウト（数秒〜10 秒）を超えるため、
> **ノンブロッキング + ポーリング推奨**。

### 2.5 更新ステータス確認 (POLL STATUS)

```
送信: F0 DE 00 00 01
受信: ステータスバイト + 9000
       └ 0x00: 完了
         0x01: 更新中
```

---

## 3. 画像データの構造

### 3.1 ピクセル → バイトのパッキング

**横方向に右から左**に、1 バイトに

- 2-color (1 bpp): 8 ピクセル
- 4-color (2 bpp): 4 ピクセル

をパックします。各バイトの最下位ビット側 (`p0`) が**右端のピクセル**:

```
// 4色の場合: 各バイトに4ピクセル
//   p0 = 表示上、右端のピクセル  (バイトのbit0-1)
//   p1 = その左隣                (bit2-3)
//   p2 = さらに左隣              (bit4-5)
//   p3 = 4ピクセルの左端         (bit6-7)
byte = p0 | (p1 << 2) | (p2 << 4) | (p3 << 6);

// 2色の場合: 各バイトに8ピクセル (右端=p0=bit0)
byte = p0 | (p1 << 1) | ... | (p7 << 7);
```

ただし**バイトの並びは「行の左端から右端へ」**: 行頭の最初のバイトは
**行の左 4 ピクセル** (4-color) または**左 8 ピクセル** (2-color) を含みます。

### 3.2 ブロック分割

パックしたデータを **2,000 バイト**ずつブロックに分割。
最後のブロックは 2,000 バイト未満になることがあります。

> **重要**: ブロックインデックス `N` は、画像データ内の
> オフセット `N × 2000` を**暗黙に**指します。可変サイズの
> ブロックを送ると画像位置がずれます。詳しくは
> [§5.4](#54-blockindex-による暗黙のオフセット) を参照。

### 3.3 LZO 圧縮

各ブロックを **LZO1X-1** (`lzo1x_1_compress`) で個別に圧縮します。
[miniLZO 2.10](http://www.oberhumer.com/opensource/lzo/) と互換です。

### 3.4 フラグメント分割

仕様上は圧縮したブロックを 250 バイトごとにフラグメント分割し、
複数の APDU で送信できることになっています。

> **実機の不具合**: 実機 (`SEAB...` 系の個体) では
> **複数フラグメントを送ると最終フラグメントで `SW=6992`** が返ります。
> このため**1 ブロック = 1 フラグメント**でしか送れません。
> 詳しくは [§5.1](#51-1-ブロック--1-フラグメント制約) を参照。

---

## 4. 送信手順

### ノンブロッキング推奨フロー

```
1. AUTH                  → 9000
2. (任意) GET INFO       → 9000 + デバイス情報
3. for each block:
     IMAGE FRAGMENT      → 9000  (P2=0x01 で 1 フラグメント送信)
4. REFRESH (P2=0x80)     → 9000  (即時応答)
5. while updating:
     POLL STATUS         → 0x01 (更新中) または 0x00 (完了)
```

### ブロッキングフロー (使わない方が良い)

```
1. AUTH
2. IMAGE FRAGMENT × N
3. REFRESH (P2=0x00)     → 9000  (応答が返るまで 30〜60 秒)
```

NFC 接続のタイムアウト中に応答が来ないリスクがあります。

---

## 5. 実機で発見した重要な制約・修正

このセクションは参考情報に明記されていなかった実機の挙動です。
実装時には必ず目を通してください。

### 5.1 長い APDU と ISO-DEP I-block チェイニング

仕様書では **「圧縮データを 250 バイトずつフラグメント分割して送る」** と規定されていますが、これには **ISO 14443-4 レイヤーでの I-block チェイニング**が必須です。

1 フラグメント=250 バイトとすると APDU は `5 (CLA/INS/P1/P2/Lc) + 2 (blockIndex/fragmentIndex) + 250 (payload) = 257 バイト` で、ATS で申告される FSC=256 を超えるため、**単一の ISO 14443-4 フレームには収まりません**。長 APDU は ISO 14443-4 の **I-block チェイニング**を使って複数の I-block に分割して送ります。

```
                       PCB         INF (APDU の一部)
I-block #1 (more=1):  [0x12]  [APDU バイト 0..249]
                            ↓ R(ACK) 応答 (例: PCB=0xA2)
I-block #2 (more=0):  [0x03]  [APDU バイト 250..256]
                            ↓ I-block 応答 (実際の APDU レスポンス)
```

**PCB の構成**:

| ビット | 値 | 意味 |
| --- | --- | --- |
| 7-6 | `0x00` | I-block (`0x80`=R-block, `0xC0`=S-block) |
| 5 | (固定) | 0 |
| 4 | `0x10` | チェイニング (1=もっと続く、0=最後) |
| 3 | `0x08` | CID 含む |
| 2 | `0x04` | NAD 含む |
| 1 | (固定) | 1 |
| 0 | `0x01` | ブロック番号 (送信ごとに 0↔1 トグル) |

具体的には:

- 中間 I-block の PCB: `0x02 | block_num | 0x10` (例: `0x12` or `0x13`)
- 最終 I-block の PCB: `0x02 | block_num` (例: `0x02` or `0x03`)

カードからの応答:

- 中間 I-block には **R(ACK) 応答** (PCB=`0xA2` or `0xA3`)
- 最終 I-block には **I-block 応答** (実際の APDU レスポンス)

**実装例 (C++)**:

```cpp
static const uint16_t INF_CHUNK = 250;
uint16_t off = 0;
while (off < apduLen) {
  uint16_t chunk = min(INF_CHUNK, apduLen - off);
  bool more = (off + chunk < apduLen);
  uint8_t pcb = 0x02 | (blockNum & 0x01) | (more ? 0x10 : 0x00);
  // ... pcb + apdu[off..off+chunk] を送信 ...
  // 中間ブロックなら R(ACK) を待つ、最終ブロックなら APDU 応答を待つ
  off += chunk;
}
```

`ezsignNFC` ライブラリの `transceiveISO14443_4()` がこれを自動的に処理します。

### 5.2 FSC (フレーム最大長) と APDU 長

ATS で `FSCI = 8 → FSC = 256 バイト` と申告されます。これは
**ISO 14443-4 の 1 フレームに乗る最大バイト数** (PCB + INF + CRC を含む) です。

実用上は次のように分けて考えます:

| レイヤー | 最大値 | 説明 |
| --- | --- | --- |
| プロトコル層 (`Lc`) | 250 バイト | 仕様書の規定。これより大きい分割は不要 |
| 1 APDU 全長 | 任意 | I-block チェイニングで自動分割 |
| 1 I-block の INF | 250 バイト程度 | FSC=256 から PCB+CRC を引いた値 |
| 1 RF フレーム | 256 バイト (FSC) | PN5180 の `sendData()` の上限 |

つまり、フラグメントサイズを 240 や 246 に下げる必要は本来ありません。
**仕様書通り 250 を使い、I-block チェイニングで長 APDU を分割するのが正解**。

### 5.3 SW = 6992 の意味

仕様書未記載のエラーコードです。発生条件:

- ISO 14443-4 フレームの最大長 (FSC=256) を超える APDU を I-block チェイニングなしで送ろうとした場合 (PN5180 等が単一フレームで送れず、データ破損するため)
- カードの内部処理 (LZO 復号 → 画面バッファへの書き込み) で整合性チェックに失敗した場合

I-block チェイニングが正しく実装されていれば、通常は発生しません。


### 5.4 blockIndex による暗黙のオフセット

カードは `blockIndex × 2000` を **画像内の byte オフセット**として暗黙に解釈します。

#### 影響

- ブロック番号を**飛ばすと**、後続のブロックが画像内でずれて表示される
- 可変サイズのブロック (例: 1000 バイトと 2000 バイトを混ぜる) も同様に
  画像が壊れる
- **常に固定 2000 バイト幅でブロックを送る**こと
- 最後のブロックは 2,000 バイト未満になることがあります (例: 2.9-inch は
  9,472 バイトなので 5 ブロック目は 1,472 バイト)

#### ブロックの送信時間

各ブロックの **送信時間は圧縮後サイズに比例**します:

- 圧縮後 36 バイト (ベタ白) → 1 フラグメント → 約 30 ms
- 圧縮後 500 バイト (細かいディザ) → 2 フラグメント → 約 60 ms
- 圧縮後 1500 バイト (写真風) → 6 フラグメント → 約 180 ms

書き換え速度を稼ぎたい場合は、できるだけ圧縮しやすい画像 (白基調、ベタ塗りが多い) を設計するのが有効です。

#### 白の代替ブロック (フォールバック)

`ezsignNFC` ライブラリには `setOversizedBlockBehavior(EZSIGN_BLOCK_SUBSTITUTE_WHITE)` というオプションがあります。I-block チェイニングが正しく動作している環境では通常不要で、フォールバック用途で提供されています。

### 5.5 ポーリング中の NFC 接続切断

画面更新中 (30〜60 秒) のポーリング中に、**NFC 接続が突然切れる**ことが
あります。原因は以下が考えられます:

- カード/PN5180 の温度ドリフト
- カードの内部スリープタイマー

#### 対処

連続ポーリング失敗時に **NFC を再活性化** (REQA → RATS のみ、AUTH は
スキップ) すれば、多くの場合復活します。`AUTH` を再送すると
**進行中のリフレッシュをリセットする**可能性があるので**送り直さない**こと。

```
ポーリング失敗 3 回連続
  → REQA + Anticoll + SELECT + RATS  (AUTH は送らない)
  → ポーリング再開
```

それでもダメなら、画像送信と REFRESH の APDU は届いているので
**ステータス確認を諦めて成功とみなす**のが実用的。

### 5.6 ブロックインデックスは 8 ビット

`blockIndex` は **uint8_t (0-255)** です。
4.2-inch 4-color (400×300, 2bpp) なら 30,000 バイト ÷ 2,000 = 15 ブロックなので
余裕で収まりますが、可変ブロック分割で増やすときは注意。

### 5.7 アンチエイリアスのない描画推奨

ディザリングや誤差拡散を使う際、**バナーや UI 要素は誤差拡散を
スキップ**してベタ塗りで描いた方が圧縮率が上がります。
誤差拡散を全面に適用すると、その後の LZO 圧縮率が著しく悪化します。

### 5.8 2.9-inch / 4.2-inch のサイズ別注意点

両サイズで共通の APDU・LZO・ブロック構造ですが、**画素数とブロック数が違う**
ため画像生成側で合わせる必要があります。

| サイズ | 解像度 | 1行のバイト数 (4色) | 画像合計 | ブロック数 (2000B) |
| --- | --- | --- | --- | --- |
| 2.9-inch | 296 × 128 | 74 | 9,472 B | **5 ブロック** (最後は 1,472 B) |
| 4.2-inch | 400 × 300 | 100 | 30,000 B | **15 ブロック** (全ブロック 2000 B) |

#### 4.2-inch の方向

公式仕様では「そのまま」となっていますが、**実機検証で左右反転が必要**だった
個体があります。`flipH=true` を初期値に推奨。

#### 2.9-inch の方向（重要）

仕様書では「90度回転」と書かれています。実装上は:

- **論理画面**: 296×128 (横長で読む向き)
- **送信画面**: 128×296 (縦長、カードのネイティブ向き)

論理座標 (x, y) → 送信座標 (txX, txY) への変換 (時計回り90°):

```cpp
// 論理座標を時計回り90度回転して送信座標へ
txX = logicalHeight - 1 - y;   // = 127 - y
txY = x;
```

このまま `sendImageIndices()` に渡すと、ライブラリは送信座標で
1行 = 128×2/8 = **32 バイト** で詰めます。9,472 バイト全体 = 5 ブロック。

##### GET INFO の応答の罠

`GET INFO` の応答で width/height がどう返るかは **個体差** があります:

- パターン A: `width=128, height=296` (縦長として返る)
- パターン B: `width=296, height=128` (横長として返る)

どちらでも 296×128 = 37,888 画素 = 9,472 バイトの 4 色画像を意味します。
パネル判別時は **両方を受け入れる**必要があります:

```cpp
if ((info.width == 128 && info.height == 296) ||
    (info.width == 296 && info.height == 128)) {
  // 2.9-inch 4-color として処理
}
```

##### 設定フラグ (V25 リファレンス実装から)

実機の個体差に対応するため、以下のフラグが有用です:

```cpp
bool CFG_29_ROTATE_CW = true;                 // 時計回り90度回転
bool CFG_29_EXTRA_FLIP_H_AFTER_ROTATE = false; // 回転後にさらに左右反転
```

ロジック:

```cpp
if (CFG_29_EXTRA_FLIP_H_AFTER_ROTATE) {
  x = logicalW - 1 - x;
}
if (CFG_29_ROTATE_CW) {
  txX = logicalH - 1 - y;
  txY = x;
} else {
  txX = y;
  txY = logicalW - 1 - x;
}
```

#### コード例

`getDeviceInfo()` から `width` と `height` を取得し、それに合わせて
画像バッファを確保するのが移植性のあるやり方です:

```cpp
EzsignDeviceInfo info;
ez.getDeviceInfo(info);
size_t pixels = (size_t)info.width * info.height;
uint8_t* indices = (uint8_t*) malloc(pixels);
// ... fill indices ...
ez.sendImageIndices(indices, info.width, info.height, /*flipH=*/true);
```

### 5.9 長い文字列の二行分割 (描画上の知見)

これはプロトコルではなく描画レイヤーの知見ですが、有用なので記載します。

Spotify のような曲名・アーティスト名を表示する場合、**長すぎる文字列の扱い**で
3 つの選択肢があります:

| 方法 | 結果 | 欠点 |
| --- | --- | --- |
| 末尾 `...` で省略 | 短い文字列に揃う | 情報が欠ける |
| 水平方向に圧縮スケール | 全文字残る | 文字が歪む |
| **2 行に均等分割** | 全文字残る・歪まない | 縦のスペースを取る |

V26 リファレンス実装では、最後の **「2 行に均等分割」** を採用しています。
分割点の選択ロジック:

1. 全ての分割位置 (split=1..N-1) について、左右の幅差 (`|wa - wb|`) を計算
2. **空白・ハイフン・全角中点・コロン**などの「分割しやすい文字」の前後で
   分割すると `score -= 20` のボーナス
3. スコア最小の分割位置を採用

「分割しやすい文字」のコードポイント:

```cpp
0x20 (半角空白), 0x3000 (全角空白)
'-', '|', '/', ':' とそれらの全角バリエーション
0x30FB (・), 0x30FC (ー), 0x2015 (―), 0xFF5E (〜)
```

これにより、`"歌詞の名前 - 別バージョン"` のような曲名が
**`"歌詞の名前"` / `"- 別バージョン"`** のように見やすく分割されます。

---

## 6. PN5180 利用時の注意点

### 6.1 電源

多くの PN5180 モジュール (DealHack 製等) は **5V と 3.3V 両方が必要**です。
データシートには「5V 単一電源」と書かれていることもありますが、
これだけでは RF アンテナを十分励起できないことがあります。

| 状態 | 症状 |
| --- | --- |
| 5V のみ接続 | SPI は通る (`PN5180::readEEPROM()` 等は動く) が、
              ATQA が返ってこない |
| 5V + 3.3V 両方接続 | ATQA、ATS、認証まで安定動作 |

**カード認識が不安定なときは、まず 3.3V も配線**してください。

### 6.2 SPI 速度と CRC

ATrappmann/PN5180-Library が内部で SPI 設定をしますが、
ISO 14443-3 (anti-collision) と ISO 14443-4 (RATS, APDU) でCRC の
オン/オフが違うことに注意:

```cpp
// REQA / Anticollision: CRC OFF (ISO14443-3 が短フレーム使うため)
nfc.writeRegisterWithAndMask(REG_CRC_TX_CONFIG, 0xFFFFFFFE);
nfc.writeRegisterWithAndMask(REG_CRC_RX_CONFIG, 0xFFFFFFFE);

// SELECT / RATS / APDU: CRC ON
nfc.writeRegisterWithOrMask(REG_CRC_TX_CONFIG, 0x00000001);
nfc.writeRegisterWithOrMask(REG_CRC_RX_CONFIG, 0x00000001);
```

`PN5180ISO14443::activateTypeA()` は内部で CRC を切り替えますが、
**RATS は標準サポートにないので自分で送る必要**があります。

### 6.3 RATS 送信 (ISO 14443-4 への昇格)

`activateTypeA()` の後、APDU を使うには **RATS** を送る必要があります:

```cpp
uint8_t rats[2] = {0xE0, 0x80};   // FSDI = 8 (FSD = 256), CID = 0
// CRC ON で送信
```

応答 (ATS) を受けたら ISO 14443-4 セッション開始。以降の APDU は
**I-Block** (PCB = `0x02 | block_number`) でラップして送ります。

### 6.4 I-Block のブロック番号トグル

ISO 14443-4 では各 I-Block にブロック番号 (0 または 1) を含めます。
**コマンド送信ごとにトグル**します:

```cpp
uint8_t pcb = 0x02 | (block_number & 0x01);
// 応答を受け取ったら:
block_number ^= 0x01;
```

セッション開始 (RATS 後) は `block_number = 0` から始めます。

### 6.5 S(WTX) 応答

カードが処理に時間がかかる場合、`S(WTX)` (Wait Time eXtension) を
返してきます (PCB = `0xF2`)。**そのまま反射して返す**必要があります:

```cpp
if ((pcb & 0xF7) == 0xF2) {  // S(WTX), CID 有無を無視
  // 同じ PCB と WTX バイトをエコー
  send_back(rx);
}
```

`ezsignNFC` ライブラリは内部で処理します。

### 6.6 PN5180 のタイムアウト・IRQ

`waitForRxIrq()` で `IRQ_RX_STAT (bit 0)` を待ちますが、
**`IRQ_RX_TIMEOUT (bit 8)` や `IRQ_ERR (bit 10)`** も同時に確認すべきです。
カードが応答しない場合これらが先に立ちます。

```cpp
uint32_t s = nfc.getIRQStatus();
if (s & (1UL << 0))  return RX_OK;
if (s & (1UL << 8))  return TIMEOUT;
if (s & (1UL << 10)) return ERROR;
```

---

## 7. ESP32 / M5Stack での実装ガイド

### 7.1 推奨環境

- **ボード**: ESP32-S3 系 (M5AtomS3, M5Stamp S3 など)
  → SRAM 512 KB あり、LZO ワーク領域 (64 KB) を確保できる
- **Arduino Core**: arduino-esp32 v2.0+
- **必須ライブラリ**:
  - `M5Unified` (M5Stack 製品の場合、Arduino Library Manager から)
  - `PN5180` (ATrappmann/PN5180-Library) — **Library Manager に無し**:
    [GitHub](https://github.com/ATrappmann/PN5180-Library) から ZIP を
    ダウンロードして「.ZIP 形式のライブラリをインストール…」で取り込む
  - `ezsignNFC` (本ライブラリ)
  - `miniLZO 2.10` — Library Manager に無し:
    [GitHub](https://github.com/yuhaoth/minilzo) から 4 ファイル
    (`minilzo.c`, `minilzo.h`, `lzoconf.h`, `lzodefs.h`) を取得し、
    **各スケッチの `.ino` と同じフォルダに配置**

### 7.2 配線例 (M5AtomS3)

```
PN5180   → M5AtomS3
─────────────────────
+5V      → +5V
+3.3V    → 3.3V       ← 必須 (§6.1)
RST      → G38
NSS      → G5
MOSI     → G8
MISO     → G6
SCK      → G7
BUSY     → G2
GND      → GND
IRQ      → (任意; 未接続でOK)
```

### 7.3 メモリ要件

| バッファ | サイズ | 用途 |
| --- | --- | --- |
| `lzo_work` | 64 KB | LZO1X-1 ワークメモリ |
| `cmpBuf`   | 約 2.2 KB | 圧縮結果スカラッチ |
| `packed`   | 30 KB (4.2-inch 4-color) | パック済み画像 |
| `dither`   | 120 KB (任意; 行ベースなら不要) | ディザ中間結果 |

合計約 100 KB。M5AtomS3 (320 KB SRAM) で十分動作。
M5Stack Atom Lite (ESP32 / 数百 KB SRAM) でも可だが、
他のライブラリ次第ではタイト。

### 7.4 SPI 共有時の注意

**M5Stack Core2 や M5StickC** は他の周辺機器 (LCD, microSD) と SPI を
共有しています。PN5180 用に**異なる SPI ピン**を割り当てるか、
SPI トランザクションを排他にする必要があります。

```cpp
// AtomS3 では他に SPI を使う機器がないので、デフォルト SPI を直接使える
SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, NSS_PIN);
```

### 7.5 タイミング・watchdog

ESP32 のタスク watchdog は通常 ~5 秒。LZO 圧縮や長いディザ処理を入れる
場合、定期的に `yield()` または `delay(1)` を入れるとリセット回避できます:

```cpp
for (int y = 0; y < H; ++y) {
  if ((y % 30) == 0) yield();
  // ...
}
```

### 7.6 サンプルコード

`examples/HelloEzsign/HelloEzsign.ino` と
`examples/SimpleImage/SimpleImage.ino` を参照。

---

## 8. トラブルシュートチェックリスト

| 症状 | 原因の候補 | 確認・対処 |
| --- | --- | --- |
| `[NFC] no card` (`activateTypeA` が失敗) | 電源不足 | PN5180 に **3.3V** も配線 (§6.1) |
| | カード位置不適切 | アンテナ中央に密着 |
| | RST/BUSY 配線間違い | ピン割り当て確認 |
| `RATS failed` | CRC 切替忘れ | SELECT 後 CRC ON にする (§6.2) |
| `AUTH SW=XXXX` (非9000) | カード未起動 / 別仕様 | デバイス UID と SAK 確認 |
| `BLOCK X SW=6992` | 複数フラグメント送信 | §5.1 圧縮率を上げる |
| `BLOCK X` 直後に通信エラー (PN5180 がフリーズ) | APDU が単一フレームに乗らない | ライブラリの ISO chaining が動作しているか確認 (§5.1) |
| 画像が**ずれて**表示 | ブロックスキップ | 白の代替を送って blockIndex 維持 (§5.4) |
| 画像が**鏡像** | flipH 設定の食い違い | `flipH = true` にする |
| ポーリングで突然タイムアウト | 接続切断 | 自動再活性化 (§5.5) |
| `[OUT] of memory` | LZO ワーク 64 KB が確保不可 | ESP32-S3 を使う / 他のメモリ削減 |

---

## 9. SONY RC-S300 (PaSoRi 4.0) + WebUSB での実装

EZ Sign は ISO 14443-4 Type A の APDU 通信なので、PN5180 以外のリーダーからも書き換えられます。本セクションは SONY **RC-S300/S** (`USB VID:PID = 054C:0DC8`) を **Chrome / Edge の WebUSB から直接叩く** 構成に必要な仕様をまとめます。`ezsign-rcs300-writer.html` が動作するリファレンス実装です。

### 9.1 リーダー要件

| 項目 | 値 |
| --- | --- |
| 対応機種 | RC-S300/S (`054C:0DC8`), RC-S300/P (`054C:0DC9`) |
| 使用インターフェース | **Interface #1** (Vendor Specific, class `0xFF`) |
| エンドポイント | EP `2 OUT` Bulk / EP `2 IN` Bulk (packet=64) |
| ブラウザ | Chrome / Edge / Opera など Chromium 系 |
| HTML の置き場所 | `file://` ローカルまたは HTTPS (iframe/sandbox 不可) |
| 事前準備 | Sony 純正常駐ソフト (NFC Port Software, FeliCa Secure Client, FeliCa Port 等) はすべて停止する |

Interface #0 は CCID クラスのため Chrome の Permission Policy で `claimInterface()` がブロックされます。**必ず Interface #1 を使うこと**。

### 9.2 USB フレームフォーマット

すべてのコマンドは **10 バイトの CCID 風ヘッダ + ペイロード** を Bulk OUT に書き込みます:

```
[0x6B] [len_LE_4B] [slot=0x00] [seq] [0x00 0x00 0x00] [payload...]
```

ペイロード (Sony 独自):

```
[0xFF 0x50 0x00] [P2] [len_BE_2B] [data...] [0x00 0x00 0x00]
```

`P2` の意味:

| P2 | 用途 |
| --- | --- |
| `0x00` | 制御プレーン (セッション開始/終了、RF on/off、SwitchProtocol など) |
| `0x01` | RF 透過送信 (生のフレームをそのまま RF に流す) |
| `0x02` | 同上 (別モード。ATS 拡張取得用) |

レスポンスは Bulk IN から `[CCID 10B ヘッダ: 0x83 len ...] [Sony payload]` 形式で返ります。Sony payload は典型的には:

- 制御コマンド: `C0 03 status SW1 SW2 [optional TLVs] 90 00`
- RF 透過: `[card data] SW1 SW2`

`status=0x00` はリーダー側成功、`status=0x01` は RF 応答無しを意味します。

### 9.3 ISO-DEP アクティベーションのバイト列

以下を順番に送信します。各コマンドの間に短い `await` (50-100ms 程度) を挟みます。`RF Off → RF On` の間は 500-800ms 開けて NFC 給電カードの起動を待ちます。

```
EndTransparent       = FF 50 00 00 02 82 00 00
StartTransparent     = FF 50 00 00 02 81 00 00
SwitchRFOff          = FF 50 00 00 02 83 00 00
SwitchRFOn           = FF 50 00 00 02 84 00 00
```

その後 SwitchProtocolTypeA を送ると ATS まで取得できます:

```
SwitchProtocol TypeA full = FF 50 00 02 04 8F 02 00 04 00
```

レスポンスに含まれる **TLV `5F 51`** が ATS です (例: `5F 51 0B 3B 86 80 01 90 BE D2 BD 1D 03 58`)。

または基本版で SwitchProtocol してから手動 RATS する方法もあります:

```
SwitchProtocol TypeA basic = FF 50 00 02 04 8F 02 00 03 00
Manual RATS  (P2=0x01 で送信) = FF 50 00 01 00 02 E0 80 00 00 00
```

### 9.4 APDU は I-Block を手動で組み立てて送る (重要)

**RC-S300 の SwitchProtocolTypeA 後の APDU 透過モード (APDU をそのまま CCID Escape で送る経路) は PCB を付けない** EZ Sign が期待する I-Block 構造にならず `6985` (conditions of use not satisfied) で拒否されます。

対処法: **CommunicateThruEX (P2=0x01)** で **PCB + APDU** を自分で組み立てて送ります:

```
FF 50 00 01 [len_BE_2B] [02 | BN] [APDU bytes...] 00 00 00
                        ─────────  ─────────────
                        PCB        ezsign 期待のバイト列
```

応答は `[02|BN] [data...]` の I-Block 形式で返るので、PCB を剥がして INF (Information Field) を取り出します。

ISO-DEP の必須要件:

- **ブロック番号** (PCB の bit0) を APDU ごとに **0↔1 でトグル**
- **R-Block** (`PCB & 0xC0 == 0x80`) 受信時は直前の I-Block を**再送**
- **S(WTX)** (`PCB & 0xF7 == 0xF2`) 受信時は同じフレームを**そのままエコーバック**してから本来の応答を待つ
- 250 バイト超の APDU はチェイニング (`PCB & 0x10` を立てて分割送信)

これらは §5.1 の M5Stack 実装と同じ要件です。

### 9.5 EZ Sign APDU の流し方 (例: AUTH)

```js
// AUTH = 00 20 00 01 04 20 09 12 10
// ブロック番号 BN=0 → PCB=0x02
// ThruEX wrapper:
//   FF 50 00 01 [len_BE_2B = 00 0A] [02 00 20 00 01 04 20 09 12 10] 00 00 00
const wrapper = [
  0xFF, 0x50, 0x00, 0x01,
  0x00, 0x0A,                              // INF len BE
  0x02, 0x00,0x20,0x00,0x01,0x04, 0x20,0x09,0x12,0x10,
  0x00, 0x00, 0x00
];
// → 期待応答: [02 90 00]   (PCB=0x02 + SW=9000)
```

GET INFO / IMAGE FRAGMENT / REFRESH / POLL STATUS も同じ仕組みで流します。APDU 本体は §3, §5 を参照。

### 9.6 LZO 圧縮

純 JavaScript ライブラリ (`lzo.js` など) で十分ですが、外部依存を避けたい場合は **LZO1X-1 の "リテラルランのみ" ストリーム**でも EZ Sign は受け付けます。圧縮率はゼロ (むしろわずかに膨張) ですが動作はします:

```
header     : (rawLen <= 238) ? (rawLen + 17) : 0x00 + (rawLen-18)/255 個の 0xFF + 余り
literals   : <raw bytes>
end marker : 11 00 00
```

### 9.7 RC-S300 + WebUSB 専用のトラブルシュート

| 症状 | 原因 | 対処 |
| --- | --- | --- |
| `claimInterface(1) failed: Unable to claim interface` | Sony 純正常駐ソフトが先に掴んでいる | NFC Port Software 系を全部停止 |
| 全 APDU が `6985` | APDU 透過モードで PCB が抜けている | CommunicateThruEX で手動 I-Block (§9.4) |
| `C0 03 01 6A 81` | RF 送信したがカード応答なし | カード位置 / アンテナとの距離 (1〜2 mm) |
| `C0 03 01 63 01` | RF タイムアウト | RF Off → 500ms → RF On でリセット |
| `67 00` (wrong length) | CommunicateThruEX の長さが 2B BE になっていない | `[len_BE_HI len_BE_LO]` の順 |
| `S(WTX)` ばかり返る | カードが LZO 展開中 | エコーバック実装で対応 (§9.4) |
| `Access disallowed by Permissions Policy` | iframe / sandbox 内で動かしている | トップレベル HTML (`file://` or HTTPS) で実行 |

### 9.8 リファレンス実装

`ezsign-rcs300-writer.html` を参照。1 枚の HTML ファイルで以下を実装しています:

- RC-S300 への WebUSB 接続 (Interface #1 の自動選択)
- §9.3 のアクティベーションシーケンス
- §9.4 の I-Block 手動構築 (PCB トグル / R-Block 再送 / S(WTX) エコー / チェイニング)
- §9.6 のリテラル LZO ストリーム
- 画像トリミング / 拡大縮小 / 4 色量子化 / 8 種類のディザリング
- AUTH / GET INFO / IMAGE FRAGMENT / REFRESH / POLL STATUS の APDU 一連送信
- 2.9-inch / 4.2-inch の自動判定 (GET INFO 経由)
- ノーマル / デバッグの 2 モード切替

`file://` で開けばそのまま動作します。

---

## 参考

- [niw 氏のリバースエンジニアリング](https://gist.github.com/niw/3885b22d502bb1e145984d41568f202d)
- [@alt-core 氏の Python 実装 nfc-eink](https://github.com/alt-core/nfc-eink)
- [PN5180 データシート](https://www.nxp.com/docs/en/data-sheet/PN5180A0XX-C3.pdf)
- [ATrappmann/PN5180-Library](https://github.com/ATrappmann/PN5180-Library)
- [LZO (miniLZO)](http://www.oberhumer.com/opensource/lzo/)
- [sakura-system.com — WebUSB で FeliCa リーダーから読み取り](https://sakura-system.com/?p=3120) — RC-S300 の Vendor Specific プロトコル解説
- [laddge/esp32-pasori-rcs300](https://github.com/laddge/esp32-pasori-rcs300) — RC-S300 を USB Host で叩く実装
