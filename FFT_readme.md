# 256x256 配列の FFT / IFFT を回すための API ガイド（RA8E1_prj）

このリポジトリには、2D FFT/IFFT の実装が `src/fft_depth_test.c` / `src/fft_depth_test.h` にまとまっています。

結論として、**256x256 の任意配列を FFT→IFFT したい場合は**、SRAM上の `fft_2d()` ではなく、**HyperRAM 上の「真の2D」実装 `fft_2d_hyperram_full()` を使う**のが安全です。

---

## まず押さえる前提（重要）

- データ型は **`float` (float32)** を前提にしています。
- 2D 配列は **行優先（row-major）** で 1 次元に並べます。
  - インデックス: `idx = y * cols + x`
- FFT/IFFT は複素数なので、データは **実部配列(real) と虚部配列(imag) の2面**で扱います。
  - 実数入力だけなら、imag 面を `0.0f` で埋めてください。
- `fft_1d_mve()` が内部で **2の冪長（power-of-two）** を要求します。
  - 256 は OK。

---

## どの API を使うべきか（用途別）

### 推奨: 256x256 を確実に回したい（HyperRAM上で処理）
- 使うAPI: `fft_2d_hyperram_full()`
- ヘッダ: `#include "fft_depth_test.h"`

```c
void fft_2d_hyperram_full(
    uint32_t hyperram_input_real_offset,
    uint32_t hyperram_input_imag_offset,
    uint32_t hyperram_output_real_offset,
    uint32_t hyperram_output_imag_offset,
    uint32_t hyperram_tmp_real_offset,
    uint32_t hyperram_tmp_imag_offset,
    int rows, int cols, bool is_inverse);
```

この関数は以下の順で処理します（rows=256, cols=256 の例）:
1) 行FFT（長さ=cols）を全行に実施 → output へ書き込み
2) output(rows x cols) を tmp(cols x rows) に転置
3) tmp の各行（長さ=rows）にFFT（= 元の列FFT）
4) tmp を output に逆転置

**ポイント:**
- `tmp_real/tmp_imag` は **rows*cols 要素分**（256x256なら各 0x40000 bytes）の別領域が必要です。
- input と output は別でも同じでも良い（いわゆる in-place でも動く設計）ですが、
  まずは **input と output を分ける**のが無難です。

### 注意: `fft_2d()` は 256x256 には向きません
- `fft_2d()` は API だけ見ると汎用に見えますが、実装が **テスト用(16x16)向けの静的列バッファ**に依存しています。
- 256x256 のような大きい行列では **列バッファサイズが足りず破綻**するため、256x256用途には使わないでください。

---

## 256x256 を FFT → IFFT する最小例（HyperRAM上）

### 1) HyperRAM 上の領域を確保（オフセットを決める）
`src/fft_depth_test.c` の 256x256 テストと同じレイアウト例です。

#### オフセット衝突に注意（カメラ/UDP/Multigridと共存する場合）

このプロジェクトでは、HyperRAMは「0x00起点の論理オフセット」で各機能が領域を使います。
**256x256 FFTは最大で約2MB（面を複数使う）を連続確保する**ので、既存の用途と被らない場所を選んでください。

既知の使用例（目安）:

- 深度復元(Multigrid)系: 低いアドレス帯を広く使用
  - `src/main_thread3_entry.c` では `FRAME_SIZE`/`GRADIENT_OFFSET`/`DEPTH_OFFSET` などを 0x00 近傍から配置し、
    `MG_WORK_OFFSET` 以降に multigrid の作業領域が連なります（`fft_depth_test.h` のコメントでも「約0x05DD00～0x18A000」が目安）。
- カメラ/UDP動画フレーム: **デフォルトで 0x300000 を使用**
  - `src/video_frame_buffer.h` の `VIDEO_FRAME_BASE_OFFSET_DEFAULT` が既定値で `3MB (0x300000)` です。
  - 1フレームは `320*240*2 = 153,600 bytes (0x25800)` なので、概ね `0x300000〜0x325800` はカメラ用に予約と考えるのが安全です。

したがって、**カメラ/UDPと共存させるなら `BASE_OFFSET=0x300000` は避け**、
例えば `0x400000` 以降に置くのが安全です（HyperRAMは8MBなのでまだ余裕があります）。

#### （オプション）カメラ領域の“直後”に自動配置する例

共存ビルドで「カメラの既定フレーム領域とFFTが被ってた…」を防ぐために、
`VIDEO_FRAME_BASE_OFFSET_DEFAULT` とフレームサイズから **FFTのBASEを自動で決める**例です。

```c
#include "video_frame_buffer.h"

enum { FFT256_SIZE = 256 };
static const uint32_t FFT256_PLANE_SIZE = 0x40000u;        // 256*256*4
static const uint32_t FFT256_PLANES     = 8u;              // 入出力+tmp等
static const uint32_t FFT256_TOTAL      = FFT256_PLANES * FFT256_PLANE_SIZE; // 0x200000

static inline uint32_t fft256_pick_base_offset(uint32_t frame_bytes)
{
  // 既定のカメラ格納領域の“直後” + 少し余白を空ける
  uint32_t base = (uint32_t)VIDEO_FRAME_BASE_OFFSET_DEFAULT;
  base = base + frame_bytes + 0x10000u; // 64KB pad（念のため）
  base = video_frame_align_u32(base);   // 16B align

  // 念のため、8MB(=HYPERRAM_SIZE)を越えないようにチェック
  if ((base + FFT256_TOTAL) > (uint32_t)HYPERRAM_SIZE)
  {
    // 収まらないなら安全側の固定値へフォールバック（運用に合わせて調整）
    base = 0x400000u;
  }
  return base;
}
```

この `base` を使って、以降の `IN_REAL/IN_IMAG/...` を `PLANE_SIZE` 単位で割り当てればOKです。
（`VIDEO_FRAME_BASE_OFFSET_STEP` を将来有効化してフレーム格納位置が動く運用にする場合も、
まずは「固定の安全領域」を決めておく方が衝突しにくいです。）

- 256x256 float 配列1面 = `256*256*4 = 0x40000 bytes`
- real/imag + output real/imag + tmp real/imag で最低 6 面必要（用途により work 面を追加する場合あり）

```c
enum { FFT_SIZE = 256 };
const uint32_t PLANE_SIZE = 0x40000;     // 256*256*4
const uint32_t BASE_OFFSET = 0x400000;   // カメラ既定(0x300000)と被らない場所に置く

const uint32_t IN_REAL  = BASE_OFFSET + 0 * PLANE_SIZE;
const uint32_t IN_IMAG  = BASE_OFFSET + 1 * PLANE_SIZE;
const uint32_t OUT_REAL = BASE_OFFSET + 2 * PLANE_SIZE;
const uint32_t OUT_IMAG = BASE_OFFSET + 3 * PLANE_SIZE;
const uint32_t TMP_REAL = BASE_OFFSET + 6 * PLANE_SIZE;
const uint32_t TMP_IMAG = BASE_OFFSET + 7 * PLANE_SIZE;
```

### 2) 入力行列を書き込む（real 面 / imag 面）
HyperRAMへは `hyperram_b_write()` を使います。

```c
#include "hyperram_integ.h"
#include "fft_depth_test.h"

static float row_real[256];
static float row_zero[256];

void write_input_256x256(const float *src_rowmajor_real)
{
    for (int i = 0; i < 256; i++) row_zero[i] = 0.0f;

    for (int y = 0; y < 256; y++)
    {
        // 1行分だけSRAMに用意して書く（大配列をSRAMに丸ごと持たない）
        for (int x = 0; x < 256; x++)
        {
            row_real[x] = src_rowmajor_real[y * 256 + x];
        }

        uint32_t off_r = IN_REAL + (uint32_t)(y * 256) * sizeof(float);
        uint32_t off_i = IN_IMAG + (uint32_t)(y * 256) * sizeof(float);

        hyperram_b_write(row_real, (void *)off_r, 256 * sizeof(float));
        hyperram_b_write(row_zero, (void *)off_i, 256 * sizeof(float));
    }
}
```

---

## 実運用に組み込む場合の“呼び出し場所”と最小ラッパ例

「任意の 256x256 配列を与えて、FFT→IFFT を回して結果を取り出す」用途なら、
`src/fft_depth_test.c` のテスト関数を直接いじるより、**自分用の関数を1つ作って**
そこから `fft_2d_hyperram_full()` を呼ぶのが分かりやすいです。

呼び出し場所の例:

- FreeRTOS の任意タスク（例: 検証スレッド / main_thread3 など）
  - 256x256 は処理時間もHyperRAM帯域もそれなりに使うので、lwIP tcpipスレッド等の“止めてはいけないスレッド”からは呼ばないのが無難です。
- 既存のテストを使うだけなら `fft_test_hyperram_256x256()` を呼ぶ
  - こちらは「内部でパターン生成→FFT→IFFT→RMSE」までやるデモ/診断用途です。

### 最小ラッパ例（in/out をSRAM配列で渡す）

この例は以下を行います:

1) `in_real_rowmajor[256*256]` を HyperRAM に row ごと書き込み（imag=0）
2) `fft_2d_hyperram_full()` で forward FFT
3) `fft_2d_hyperram_full()` で inverse FFT
4) 結果（復元後の real）を `out_real_rowmajor[256*256]` へ row ごと読み出し

```c
#include "fft_depth_test.h"
#include "video_frame_buffer.h"

enum { N = 256 };

static void fft256_roundtrip(const float *in_real_rowmajor, float *out_real_rowmajor)
{
    const uint32_t frame_bytes = (uint32_t)(VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL);
    const uint32_t base = fft256_pick_base_offset(frame_bytes);

    const uint32_t PLANE_SIZE = 0x40000u; // 256*256*4
    const uint32_t IN_REAL  = base + 0u * PLANE_SIZE;
    const uint32_t IN_IMAG  = base + 1u * PLANE_SIZE;
    const uint32_t OUT_REAL = base + 2u * PLANE_SIZE;
    const uint32_t OUT_IMAG = base + 3u * PLANE_SIZE;
    const uint32_t TMP_REAL = base + 6u * PLANE_SIZE;
    const uint32_t TMP_IMAG = base + 7u * PLANE_SIZE;

    static float row_real[256];
    static float row_zero[256];
    for (int i = 0; i < N; i++) row_zero[i] = 0.0f;

    // 入力を書き込み（row単位）
    for (int y = 0; y < N; y++)
    {
        for (int x = 0; x < N; x++)
        {
            row_real[x] = in_real_rowmajor[y * N + x];
        }

        uint32_t off_r = IN_REAL + (uint32_t)(y * N) * sizeof(float);
        uint32_t off_i = IN_IMAG + (uint32_t)(y * N) * sizeof(float);
        hyperram_b_write(row_real, (void *)off_r, (uint32_t)N * sizeof(float));
        hyperram_b_write(row_zero, (void *)off_i, (uint32_t)N * sizeof(float));
    }

    // FFT（forward）
    fft_2d_hyperram_full(IN_REAL, IN_IMAG,
                         OUT_REAL, OUT_IMAG,
                         TMP_REAL, TMP_IMAG,
                         N, N, false);

    // IFFT（inverse）: 復元結果を IN_* 側に戻す例
    fft_2d_hyperram_full(OUT_REAL, OUT_IMAG,
                         IN_REAL, IN_IMAG,
                         TMP_REAL, TMP_IMAG,
                         N, N, true);

    // 出力を読み出し（row単位）
    for (int y = 0; y < N; y++)
    {
        uint32_t off = IN_REAL + (uint32_t)(y * N) * sizeof(float);
        hyperram_b_read(row_real, (void *)off, (uint32_t)N * sizeof(float));
        for (int x = 0; x < N; x++)
        {
            out_real_rowmajor[y * N + x] = row_real[x];
        }
    }
}
```

メモ:

- `frame_bytes` の計算に `VGA_WIDTH/VGA_HEIGHT/BYTE_PER_PIXEL` を使っています（カメラ設定と合わせる前提）。
  カメラ無関係にFFTだけ回す用途なら、`fft256_pick_base_offset()` を使わずに固定の安全領域（例: `0x400000`）を使ってもOKです。
- 256x256 の FFT/IFFT は実行時間が長めなので、必要ならタスク側で優先度/周期を調整してください。

### 3) Forward FFT（256x256）

```c
fft_2d_hyperram_full(
    IN_REAL, IN_IMAG,
    OUT_REAL, OUT_IMAG,
    TMP_REAL, TMP_IMAG,
    256, 256,
    false  // forward
);
```

### 4) Inverse FFT（256x256）
IFFT結果を別領域に出したい場合は output を変えて呼びます。

```c
// 例: IFFTの出力を IN_* に戻す（in-place復元）でもOK
fft_2d_hyperram_full(
    OUT_REAL, OUT_IMAG,
    IN_REAL, IN_IMAG,
    TMP_REAL, TMP_IMAG,
    256, 256,
    true   // inverse
);
```

### 5) 結果を読む
HyperRAMからの読み出しは `hyperram_b_read()`。

```c
static float out_row[256];

void read_row(int y)
{
    uint32_t off = IN_REAL + (uint32_t)(y * 256) * sizeof(float);
    hyperram_b_read(out_row, (void *)off, 256 * sizeof(float));
}
```

---

## スケーリング（FFT→IFFTで元に戻る？）

この実装は `fft_1d_mve(..., is_inverse=true)` の内部で逆変換時にスケールを入れています。
- 256点の逆FFT（1D）は概ね `1/256` が掛かります。
- 2D逆FFTは「行」と「列」でそれぞれ逆FFTするので、全体では概ね `1/(rows*cols)` が掛かり、
  Forward→Inverse で元の値に戻る設計です（誤差は浮動小数と f16 経路等で出ます）。

---

## 参考: 既存の 256x256 テスト

- 既存の実装例は `fft_test_hyperram_256x256()`（`src/fft_depth_test.c`）がそのまま使えます。
  - 入力生成 → HyperRAM書き込み → `fft_2d_hyperram_full()`でFFT → `fft_2d_hyperram_full()`でIFFT → RMSE評価

---

## よくある落とし穴

- **オフセット領域の重なり**: real/imag/tmp が重なると壊れます。面ごとに必ず分離してください。
- **サイズ制約**: `fft_2d_hyperram_full()` は rows/cols ともに `<=256` のチェックがあります。
- **2の冪**: 256以外にしたい場合も rows/cols は power-of-two を前提にしてください。
- **`fft_2d()` の列バッファ**: 16x16テスト用の作りなので 256x256用途には不適です。

