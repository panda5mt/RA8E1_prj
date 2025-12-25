#include "main_thread3.h"
#include "hyperram_integ.h"
#include "putchar_ra8usb.h"
#include <string.h>
#include <math.h>

// ========== 深度復元アルゴリズム切り替え ==========
// 1 = マルチグリッド版（ポアソン方程式反復解法、中品質、中速: ~0.5-2秒/フレーム）
// 0 = 簡易版（行方向積分、低品質、高速: <1ms/フレーム）
#define USE_DEPTH_METHOD 1

// HyperRAMから直接p勾配をストリーミングして行積分する簡易版。
// USE_SIMPLE_DIRECT_P=1で有効化。
// USE_SIMPLE_DIRECT_P=0で従来のSRAMバッファ経由
#define USE_SIMPLE_DIRECT_P 1
// =================================================

// Helium MVE (ARM M-profile Vector Extension) support
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE > 0)
#include <arm_mve.h>
#define USE_HELIUM_MVE 1 // シンプルで安全なMVE実装
#else
#define USE_HELIUM_MVE 0
#endif

#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 2) // YUV422 = 2 bytes/pixel
#define GRADIENT_OFFSET FRAME_SIZE                  // p,q勾配マップを配置（2チャンネル×8bit）
#define DEPTH_OFFSET (FRAME_SIZE * 2)               // 深度マップ（8bit grayscale: 320×240 = 76,800バイト）

#if USE_DEPTH_METHOD == 1
#define MG_WORK_OFFSET (DEPTH_OFFSET + FRAME_WIDTH * FRAME_HEIGHT)
#define MG_MAX_LEVELS 6

typedef struct
{
    uint32_t z_offset;
    uint32_t rhs_offset;
    int width;
    int height;
} mg_level_t;

static mg_level_t g_mg_levels[MG_MAX_LEVELS];
static int g_mg_level_count = 0;
static uint32_t g_mg_residual_offset = 0;
static bool g_mg_layout_ready = false;
#endif

// Shape from Shading 光源パラメータ（定数）
#define LIGHT_PS 0.0f // 光源方向x成分
#define LIGHT_QS 0.0f // 光源方向y成分
#define LIGHT_TS 1.0f // 光源方向z成分（正規化された垂直光源）

/* YUV422からY成分（輝度）を抽出
 * YUV422フォーマット: [V0 Y1 U0 Y0] (リトルエンディアン)
 */
#if USE_HELIUM_MVE
static void extract_y_component(uint8_t *yuv_line, uint8_t *y_line, int width)
{
    // MVEベクトル化: 16ピクセル（32バイト）単位で処理
    // YUV422: [V0 Y1 U0 Y0] [V1 Y3 U1 Y2] (4バイト=2ピクセル)
    // Y成分位置: byte[3]=Y0, byte[1]=Y1, byte[7]=Y2, byte[5]=Y3, ...

    int x;
    for (x = 0; x < width - 15; x += 16)
    {
        // 32バイト読み込み（16ピクセル分のYUV422データ）
        uint8x16_t yuv_low = vld1q_u8(&yuv_line[x * 2]);
        uint8x16_t yuv_high = vld1q_u8(&yuv_line[x * 2 + 16]);

        // Y成分を抽出（MVE効率的処理）
        // Y0,Y1,Y2,Y3,...,Y15の順に並べる
        uint8_t temp[16];
        temp[0] = yuv_low[3];    // Y0
        temp[1] = yuv_low[1];    // Y1
        temp[2] = yuv_low[7];    // Y2
        temp[3] = yuv_low[5];    // Y3
        temp[4] = yuv_low[11];   // Y4
        temp[5] = yuv_low[9];    // Y5
        temp[6] = yuv_low[15];   // Y6
        temp[7] = yuv_low[13];   // Y7
        temp[8] = yuv_high[3];   // Y8
        temp[9] = yuv_high[1];   // Y9
        temp[10] = yuv_high[7];  // Y10
        temp[11] = yuv_high[5];  // Y11
        temp[12] = yuv_high[11]; // Y12
        temp[13] = yuv_high[9];  // Y13
        temp[14] = yuv_high[15]; // Y14
        temp[15] = yuv_high[13]; // Y15

        // MVEベクトルロード/ストアで高速化
        uint8x16_t y_result = vld1q_u8(temp);
        vst1q_u8(&y_line[x], y_result);
    }

    // 残りをスカラー処理
    for (; x < width; x += 2)
    {
        int yuv_index = x * 2;
        y_line[x] = yuv_line[yuv_index + 3];     // Y0
        y_line[x + 1] = yuv_line[yuv_index + 1]; // Y1
    }
}
#else
static void extract_y_component(uint8_t *yuv_line, uint8_t *y_line, int width)
{
    for (int x = 0; x < width; x += 2)
    {
        int yuv_index = x * 2;                   // 2ピクセル = 4バイト
        y_line[x] = yuv_line[yuv_index + 3];     // Y0
        y_line[x + 1] = yuv_line[yuv_index + 1]; // Y1
    }
}
#endif

/* Sobelフィルタでエッジ検出
 * 入力: 3行分のY成分（前の行、現在の行、次の行）
 * 出力: エッジ強度（0-255）
 */

#if USE_HELIUM_MVE
// Helium MVE版 - シンプルで安全な実装（スカラー計算 + ベクトル後処理）
static void apply_sobel_filter(uint8_t y_prev[FRAME_WIDTH],
                               uint8_t y_curr[FRAME_WIDTH],
                               uint8_t y_next[FRAME_WIDTH],
                               uint8_t edge_out[FRAME_WIDTH])
{
    // 境界ピクセルは中央ピクセルの値をそのまま使う
    edge_out[0] = y_curr[0];
    edge_out[FRAME_WIDTH - 1] = y_curr[FRAME_WIDTH - 1];

    // 8ピクセル単位で処理（スカラー計算 + MVEベクトル後処理）
    int x;
    for (x = 1; x < FRAME_WIDTH - 8; x += 8)
    {
        // 8ピクセル分のSobel演算をスカラーで計算
        int16_t gx_buf[8] __attribute__((aligned(16)));
        int16_t gy_buf[8] __attribute__((aligned(16)));

        for (int i = 0; i < 8; i++)
        {
            int px = x + i;

            // Sobel X勾配（スカラー計算）
            int gx = -1 * y_prev[px - 1] + 1 * y_prev[px + 1] +
                     -2 * y_curr[px - 1] + 2 * y_curr[px + 1] +
                     -1 * y_next[px - 1] + 1 * y_next[px + 1];

            // Sobel Y勾配（スカラー計算）
            int gy = -1 * y_prev[px - 1] - 2 * y_prev[px] - 1 * y_prev[px + 1] +
                     1 * y_next[px - 1] + 2 * y_next[px] + 1 * y_next[px + 1];

            gx_buf[i] = (int16_t)gx;
            gy_buf[i] = (int16_t)gy;
        }

        // ベクトルロード（MVE）
        int16x8_t gx_vec = vld1q_s16(gx_buf);
        int16x8_t gy_vec = vld1q_s16(gy_buf);

        // ベクトル演算で後処理（MVE）
        // 1. 絶対値
        int16x8_t abs_gx = vabsq_s16(gx_vec);
        int16x8_t abs_gy = vabsq_s16(gy_vec);

        // 2. 勾配の大きさ近似: (|gx| + |gy|) / 2
        int16x8_t magnitude = vaddq_s16(abs_gx, abs_gy);
        magnitude = vshrq_n_s16(magnitude, 1); // /2

        // 3. 閾値処理: magnitude < 20 → 0
        mve_pred16_t mask = vcmpgtq_n_s16(magnitude, 19);
        magnitude = vpselq_s16(magnitude, vdupq_n_s16(0), mask);

        // 4. 上限クランプ: magnitude > 255 → 255
        magnitude = vminq_s16(magnitude, vdupq_n_s16(255));

        // 5. 結果をスカラーバッファに保存
        int16_t result_buf[8] __attribute__((aligned(16)));
        vst1q_s16(result_buf, magnitude);

        // 6. 8ビットに変換して出力
        for (int i = 0; i < 8; i++)
        {
            edge_out[x + i] = (uint8_t)result_buf[i];
        }
    }

    // 残りをスカラー処理
    for (; x < FRAME_WIDTH - 1; x++)
    {
        int gx = 0, gy = 0;

        // Sobel X
        gx += -1 * y_prev[x - 1] + 1 * y_prev[x + 1];
        gx += -2 * y_curr[x - 1] + 2 * y_curr[x + 1];
        gx += -1 * y_next[x - 1] + 1 * y_next[x + 1];

        // Sobel Y
        gy += -1 * y_prev[x - 1] - 2 * y_prev[x] - 1 * y_prev[x + 1];
        gy += 1 * y_next[x - 1] + 2 * y_next[x] + 1 * y_next[x + 1];

        int magnitude = (abs(gx) + abs(gy)) / 2;
        if (magnitude < 20)
            magnitude = 0;
        else if (magnitude > 255)
            magnitude = 255;

        edge_out[x] = (uint8_t)magnitude;
    }
}

#else
// 標準版 - Helium MVEなし
static void apply_sobel_filter(uint8_t y_prev[FRAME_WIDTH],
                               uint8_t y_curr[FRAME_WIDTH],
                               uint8_t y_next[FRAME_WIDTH],
                               uint8_t edge_out[FRAME_WIDTH])
{
    const int sobel_x[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    const int sobel_y[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};

    // 境界ピクセルは中央ピクセルの値をそのまま使う
    edge_out[0] = y_curr[0];
    edge_out[FRAME_WIDTH - 1] = y_curr[FRAME_WIDTH - 1];

    for (int x = 1; x < FRAME_WIDTH - 1; x++)
    {
        int gx = 0, gy = 0;

        // 3x3カーネルを適用
        uint8_t *rows[3] = {y_prev, y_curr, y_next};
        for (int ky = 0; ky < 3; ky++)
        {
            for (int kx = 0; kx < 3; kx++)
            {
                int pixel_x = x - 1 + kx;
                int idx = ky * 3 + kx;
                gx += sobel_x[idx] * rows[ky][pixel_x];
                gy += sobel_y[idx] * rows[ky][pixel_x];
            }
        }

        // 勾配の大きさ
        int magnitude = (int)sqrtf((float)(gx * gx + gy * gy));

        // 閾値処理とスケーリング
        magnitude = magnitude / 2; // 強度を1/2に
        if (magnitude < 20)        // 閾値以下は0（ノイズ除去）
            magnitude = 0;
        else if (magnitude > 255)
            magnitude = 255;

        edge_out[x] = (uint8_t)magnitude;
    }
}
#endif // USE_HELIUM_MVE

/* Shape from Shading用のp,q勾配を計算
 * p = ∂z/∂x (x方向の表面勾配)
 * q = ∂z/∂y (y方向の表面勾配)
 *
 * 反射率方程式: R(x,y) = ρ(x,y) * (p*ps + q*qs + 1) / sqrt((1+p²+q²)(1+ps²+qs²+ts²))
 * 光源定数の場合、正規化輝度 E = R/ρ から:
 * E * sqrt(1+p²+q²) * sqrt(1+ps²+qs²+ts²) = p*ps + q*qs + 1
 *
 * 簡易推定: I(x,y) ≈ (1 - p*Gx - q*Gy) として、ローカル勾配から推定
 */
static void compute_pq_gradients(uint8_t y_prev[FRAME_WIDTH], uint8_t y_curr[FRAME_WIDTH],
                                 uint8_t y_next[FRAME_WIDTH], uint8_t pq_out[FRAME_WIDTH * 2])
{
    // Sobelカーネルで輝度勾配を計算
    const int sobel_x[9] = {
        -1, 0, 1,
        -2, 0, 2,
        -1, 0, 1};
    const int sobel_y[9] = {
        -1, -2, -1,
        0, 0, 0,
        1, 2, 1};

    // 最初と最後のピクセルは0に設定
    pq_out[0] = 0;                   // q[0]
    pq_out[1] = 0;                   // p[0]
    pq_out[FRAME_WIDTH * 2 - 2] = 0; // q[last]
    pq_out[FRAME_WIDTH * 2 - 1] = 0; // p[last]

    // 勾配を計算
    for (int x = 1; x < FRAME_WIDTH - 1; x++)
    {
        int gx = 0, gy = 0;

        // 3x3カーネルを適用
        uint8_t *rows[3] = {y_prev, y_curr, y_next};
        for (int ky = 0; ky < 3; ky++)
        {
            for (int kx = 0; kx < 3; kx++)
            {
                int pixel_x = x - 1 + kx;
                int idx = ky * 3 + kx;
                gx += sobel_x[idx] * rows[ky][pixel_x];
                gy += sobel_y[idx] * rows[ky][pixel_x];
            }
        }

        // 勾配をスケーリング: -127〜+127の範囲に正規化
        // Sobel出力範囲: 約±1020 → ÷8でスケーリング
        int p = gx / 8;
        int q = gy / 8;

        // 範囲制限
        if (p < -127)
            p = -127;
        if (p > 127)
            p = 127;
        if (q < -127)
            q = -127;
        if (q > 127)
            q = 127;

        // 符号なし8ビットに変換（0=中央値、-127→0、+127→254）
        pq_out[x * 2] = (uint8_t)(q + 127);     // q成分
        pq_out[x * 2 + 1] = (uint8_t)(p + 127); // p成分
    }
}

/* 簡易深度復元（行方向積分）
 * p,q勾配から深度マップを生成
 * 簡易版：行ごとにp勾配を積分（∫p dx）
 */
#if USE_HELIUM_MVE
// Helium MVE版 - 真のベクトル命令による高速化
static void reconstruct_depth_simple(uint8_t pq_data[FRAME_WIDTH * 2], uint8_t depth_line[FRAME_WIDTH])
{
    float z = 0.0f;           // 深度の累積値
    const float scale = 2.0f; // スケーリングファクタ

    // 16ピクセル単位でMVEベクトル処理（128-bit）
    int x;
    for (x = 0; x < FRAME_WIDTH - 15; x += 16)
    {
        // 16ピクセル分のp勾配を抽出（128-bit MVEロード用）
        uint8_t p_bytes[16] __attribute__((aligned(16)));
        for (int i = 0; i < 16; i++)
        {
            p_bytes[i] = pq_data[(x + i) * 2 + 1]; // p成分
        }

        // MVEベクトルロード（128-bit = 16バイト）
        uint8x16_t p_u8_vec = vld1q_u8(p_bytes);

        // uint8 → int16 拡張（MVE: 下位8バイト）
        int16x8_t p_s16_low = vreinterpretq_s16_u16(vmovlbq_u8(p_u8_vec));

        // -127オフセット適用（MVEベクトル減算）
        int16x8_t offset = vdupq_n_s16(127);
        int16x8_t p_adjusted_low = vsubq_s16(p_s16_low, offset);

        // ベクトルをスカラーに展開
        int16_t p_raw[8] __attribute__((aligned(16)));
        vst1q_s16(p_raw, p_adjusted_low);

        // 積分処理（前半8ピクセル）
        for (int i = 0; i < 8; i++)
        {
            z += (float)p_raw[i] * scale;

            // 積分値が極端になりすぎないように制限
            if (z < -200.0f)
                z = -200.0f;
            if (z > 200.0f)
                z = 200.0f;

            int depth_val = (int)(z + 128.0f);
            if (depth_val < 0)
                depth_val = 0;
            if (depth_val > 255)
                depth_val = 255;

            depth_line[x + i] = (uint8_t)depth_val;
        }

        // uint8 → int16 拡張（MVE: 上位8バイト）
        int16x8_t p_s16_high = vreinterpretq_s16_u16(vmovltq_u8(p_u8_vec));
        int16x8_t p_adjusted_high = vsubq_s16(p_s16_high, offset);
        vst1q_s16(p_raw, p_adjusted_high);

        // 積分処理（後半8ピクセル）
        for (int i = 0; i < 8; i++)
        {
            z += (float)p_raw[i] * scale;

            // 積分値が極端になりすぎないように制限
            if (z < -200.0f)
                z = -200.0f;
            if (z > 200.0f)
                z = 200.0f;

            int depth_val = (int)(z + 128.0f);
            if (depth_val < 0)
                depth_val = 0;
            if (depth_val > 255)
                depth_val = 255;

            depth_line[x + 8 + i] = (uint8_t)depth_val;
        }
    }

    // 残りをスカラー処理
    for (; x < FRAME_WIDTH; x++)
    {
        int p_raw = (int)pq_data[x * 2 + 1] - 127;
        z += (float)p_raw * scale;

        // 積分値が極端になりすぎないように制限
        if (z < -200.0f)
            z = -200.0f;
        if (z > 200.0f)
            z = 200.0f;

        int depth_val = (int)(z + 128.0f);
        if (depth_val < 0)
            depth_val = 0;
        if (depth_val > 255)
            depth_val = 255;

        depth_line[x] = (uint8_t)depth_val;
    }
}
#else
// 標準版（MVEなし）
static void reconstruct_depth_simple(uint8_t pq_data[FRAME_WIDTH * 2], uint8_t depth_line[FRAME_WIDTH])
{
    // p勾配を符号付きに戻す（0〜254 → -127〜+127）
    float z = 0.0f;           // 深度の累積値
    const float scale = 2.0f; // スケーリングファクタ

    for (int x = 0; x < FRAME_WIDTH; x++)
    {
        // p勾配を取得（偶数インデックス）
        int p_raw = (int)pq_data[x * 2 + 1] - 127; // -127〜+127

        // 深度を積分（z += p * dx、ここでdx=1ピクセル）
        z += (float)p_raw * scale;

        // 0〜255の範囲にマッピング
        int depth_val = (int)(z + 128.0f);
        if (depth_val < 0)
            depth_val = 0;
        if (depth_val > 255)
            depth_val = 255;

        depth_line[x] = (uint8_t)depth_val;
    }
}
#endif

#if USE_SIMPLE_DIRECT_P
/* HyperRAMから直接p勾配をストリーミングして行積分する簡易版。
 * USE_SIMPLE_DIRECT_P=0で従来のSRAMバッファ経由に戻せる。 */
static void reconstruct_depth_simple_direct(uint32_t gradient_line_offset, uint8_t depth_line[FRAME_WIDTH])
{
    float z = 0.0f;
    const float scale = 2.0f;
    uint8_t chunk[128];
    uint32_t remaining = FRAME_WIDTH * 2;
    uint32_t offset = 0;
    int pixel = 0;

    while (remaining > 0 && pixel < FRAME_WIDTH)
    {
        uint32_t chunk_size = (remaining > sizeof(chunk)) ? (uint32_t)sizeof(chunk) : remaining;
        chunk_size &= ~1U; // 偶数バイト境界を維持

        fsp_err_t err = hyperram_b_read(chunk, (void *)(gradient_line_offset + offset), chunk_size);
        if (FSP_SUCCESS != err)
        {
            xprintf("[Thread3] Direct depth read failed at offset=%lu err=%d\n",
                    (unsigned long)(gradient_line_offset + offset), err);
            memset(depth_line + pixel, 0, FRAME_WIDTH - pixel);
            return;
        }

        for (uint32_t i = 0; i < chunk_size && pixel < FRAME_WIDTH; i += 2)
        {
            int p_raw = (int)chunk[i + 1] - 127;
            z += (float)p_raw * scale;

            if (z < -200.0f)
            {
                z = -200.0f;
            }
            else if (z > 200.0f)
            {
                z = 200.0f;
            }

            int depth_val = (int)(z + 128.0f);
            if (depth_val < 0)
            {
                depth_val = 0;
            }
            else if (depth_val > 255)
            {
                depth_val = 255;
            }

            depth_line[pixel++] = (uint8_t)depth_val;
        }

        remaining -= chunk_size;
        offset += chunk_size;
    }
}
#endif

/* Frankot-Chellappa法による深度復元（FFTベース）
 * 分離可能FFTで実装（メモリ効率化）
 * p,q勾配から2D FFTで深度を計算
 */
/* エッジ強度をYUV422形式に変換（MATLAB yuv422_to_rgb_fast完全対応）
 * Y = エッジ強度, U = V = 128（グレースケール）
 * YUV422フォーマット（リトルエンディアン）:
 *   8バイト = 4ピクセル
 *   [V0 Y1 U0 Y0] [V1 Y3 U1 Y2]
 * MATLAB yuv422_to_rgb_fast デコード順序:
 *   block(8)=Y2 → pix1, block(6)=Y3 → pix2, block(4)=Y0 → pix3, block(2)=Y1 → pix4
 * つまり: pix1=edge[x], pix2=edge[x+1], pix3=edge[x+2], pix4=edge[x+3]
 *
 * 必要なエンコード:
 *   block(2)=Y1=edge[x+3], block(4)=Y0=edge[x+2], block(6)=Y3=edge[x+1], block(8)=Y2=edge[x]
 */
static void edge_to_yuv422(uint8_t edge_line[FRAME_WIDTH], uint8_t yuv_line[FRAME_WIDTH * 2])
{
    // 4ピクセル（8バイト）単位で処理
    for (int x = 0; x < FRAME_WIDTH; x += 4)
    {
        int yuv_index = x * 2; // 8バイト単位のオフセット

        // MATLABのデコード: pix1=Y2, pix2=Y3, pix3=Y0, pix4=Y1
        // edge_line順で出力するため: Y2=edge[x], Y3=edge[x+1], Y0=edge[x+2], Y1=edge[x+3]

        // 1つ目の4バイト: [V0 Y1 U0 Y0]
        yuv_line[yuv_index + 0] = 128;              // V0 = 128 (pix3,4で使用)
        yuv_line[yuv_index + 1] = edge_line[x + 3]; // Y1 = edge[x+3] → pix4
        yuv_line[yuv_index + 2] = 128;              // U0 = 128 (pix3,4で使用)
        yuv_line[yuv_index + 3] = edge_line[x + 2]; // Y0 = edge[x+2] → pix3

        // 2つ目の4バイト: [V1 Y3 U1 Y2]
        yuv_line[yuv_index + 4] = 128;              // V1 = 128 (pix1,2で使用)
        yuv_line[yuv_index + 5] = edge_line[x + 1]; // Y3 = edge[x+1] → pix2
        yuv_line[yuv_index + 6] = 128;              // U1 = 128 (pix1,2で使用)
        yuv_line[yuv_index + 7] = edge_line[x];     // Y2 = edge[x] → pix1
    }
}

// ========== マルチグリッド法による深度復元 ==========
#if USE_DEPTH_METHOD == 2

static const int pre_smooth = 2;
static const int post_smooth = 2;
static const int coarse_iter = 10;
static const int mg_cycles = 3;

static void mg_prepare_layout(void)
{
    if (g_mg_layout_ready)
    {
        return;
    }

    uint32_t offset = MG_WORK_OFFSET;
    int width = FRAME_WIDTH;
    int height = FRAME_HEIGHT;
    g_mg_level_count = 0;

    while (g_mg_level_count < MG_MAX_LEVELS)
    {
        mg_level_t *level = &g_mg_levels[g_mg_level_count];
        level->width = width;
        level->height = height;
        level->z_offset = offset;
        offset += (uint32_t)(width * height * sizeof(float));
        level->rhs_offset = offset;
        offset += (uint32_t)(width * height * sizeof(float));

        g_mg_level_count++;

        if (width <= 40 || height <= 30)
        {
            break;
        }

        width = (width + 1) / 2;
        height = (height + 1) / 2;
    }

    g_mg_residual_offset = offset;
    g_mg_layout_ready = true;
}

static void mg_zero_buffer(uint32_t offset, size_t length)
{
    uint8_t zero_block[128] = {0};
    size_t written = 0;

    while (written < length)
    {
        size_t chunk = length - written;
        if (chunk > sizeof(zero_block))
        {
            chunk = sizeof(zero_block);
        }

        hyperram_b_write(zero_block, (void *)(offset + written), (uint32_t)chunk);
        written += chunk;
    }
}

static void mg_zero_all_levels(void)
{
    for (int i = 0; i < g_mg_level_count; i++)
    {
        size_t bytes = (size_t)g_mg_levels[i].width * g_mg_levels[i].height * sizeof(float);
        mg_zero_buffer(g_mg_levels[i].z_offset, bytes);
    }
}

static void mg_compute_divergence_to_hyperram(const mg_level_t *level)
{
    const int width = level->width;
    const int height = level->height;
    const uint32_t pq_row_bytes = FRAME_WIDTH * 2;
    const uint32_t rhs_row_bytes = (uint32_t)width * sizeof(float);

    uint8_t pq_prev[FRAME_WIDTH * 2];
    uint8_t pq_curr[FRAME_WIDTH * 2];
    uint8_t pq_next[FRAME_WIDTH * 2];
    float div_row[FRAME_WIDTH];

    hyperram_b_read(pq_curr, (void *)GRADIENT_OFFSET, pq_row_bytes);
    memcpy(pq_prev, pq_curr, pq_row_bytes);
    if (height > 1)
    {
        hyperram_b_read(pq_next, (void *)(GRADIENT_OFFSET + pq_row_bytes), pq_row_bytes);
    }
    else
    {
        memcpy(pq_next, pq_curr, pq_row_bytes);
    }

    for (int y = 0; y < height; y++)
    {
        if (y == 0 || y == height - 1)
        {
            memset(div_row, 0, rhs_row_bytes);
        }
        else
        {
            div_row[0] = 0.0f;
            div_row[width - 1] = 0.0f;

            for (int x = 1; x < width - 1; x++)
            {
                int p_curr = (int)pq_curr[x * 2 + 1] - 127;
                int p_prev = (int)pq_curr[(x - 1) * 2 + 1] - 127;
                int q_curr = (int)pq_curr[x * 2] - 127;
                int q_prev = (int)pq_prev[x * 2] - 127;
                div_row[x] = -(float)(p_curr - p_prev + q_curr - q_prev);
            }
        }

        hyperram_b_write(div_row, (void *)(level->rhs_offset + y * rhs_row_bytes), rhs_row_bytes);

        if (y == height - 1)
        {
            break;
        }

        memcpy(pq_prev, pq_curr, pq_row_bytes);
        memcpy(pq_curr, pq_next, pq_row_bytes);

        int next_row = y + 2;
        if (next_row >= height)
        {
            memcpy(pq_next, pq_curr, pq_row_bytes);
        }
        else
        {
            hyperram_b_read(pq_next, (void *)(GRADIENT_OFFSET + next_row * pq_row_bytes), pq_row_bytes);
        }
    }
}

static void mg_gauss_seidel(const mg_level_t *level, int iterations)
{
    const int width = level->width;
    const int height = level->height;

    if (width < 2 || height < 2)
    {
        return;
    }

    const uint32_t row_bytes = (uint32_t)width * sizeof(float);
    float row_prev[FRAME_WIDTH];
    float row_curr[FRAME_WIDTH];
    float row_next[FRAME_WIDTH];
    float rhs_curr[FRAME_WIDTH];

    for (int iter = 0; iter < iterations; iter++)
    {
        for (int color = 0; color < 2; color++)
        {
            hyperram_b_read(row_prev, (void *)(level->z_offset + 0), row_bytes);
            hyperram_b_read(row_curr, (void *)(level->z_offset + row_bytes), row_bytes);

            if (height > 2)
            {
                hyperram_b_read(row_next, (void *)(level->z_offset + 2 * row_bytes), row_bytes);
            }
            else
            {
                memcpy(row_next, row_curr, row_bytes);
            }

            for (int y = 1; y < height - 1; y++)
            {
                hyperram_b_read(rhs_curr, (void *)(level->rhs_offset + y * row_bytes), row_bytes);
                int start_x = 1 + ((y + color) & 1);
                for (int x = start_x; x < width - 1; x += 2)
                {
                    float neighbor_sum = row_curr[x - 1] + row_curr[x + 1] + row_prev[x] + row_next[x];
                    row_curr[x] = 0.25f * (neighbor_sum - rhs_curr[x]);
                }

                hyperram_b_write(row_curr, (void *)(level->z_offset + y * row_bytes), row_bytes);

                if (y < height - 2)
                {
                    memcpy(row_prev, row_curr, row_bytes);
                    memcpy(row_curr, row_next, row_bytes);
                    int next_index = y + 2;
                    if (next_index < height)
                    {
                        hyperram_b_read(row_next, (void *)(level->z_offset + next_index * row_bytes), row_bytes);
                    }
                    else
                    {
                        memcpy(row_next, row_curr, row_bytes);
                    }
                }
            }
        }
    }
}

static void mg_compute_residual(const mg_level_t *level, uint32_t residual_offset)
{
    const int width = level->width;
    const int height = level->height;
    const uint32_t row_bytes = (uint32_t)width * sizeof(float);

    float row_prev[FRAME_WIDTH];
    float row_curr[FRAME_WIDTH];
    float row_next[FRAME_WIDTH];
    float rhs_curr[FRAME_WIDTH];
    float res_row[FRAME_WIDTH];

    if (height == 0)
    {
        return;
    }

    hyperram_b_read(row_prev, (void *)(level->z_offset + 0), row_bytes);
    if (height > 1)
    {
        hyperram_b_read(row_curr, (void *)(level->z_offset + row_bytes), row_bytes);
    }
    else
    {
        memcpy(row_curr, row_prev, row_bytes);
    }

    if (height > 2)
    {
        hyperram_b_read(row_next, (void *)(level->z_offset + 2 * row_bytes), row_bytes);
    }
    else
    {
        memcpy(row_next, row_curr, row_bytes);
    }

    memset(res_row, 0, row_bytes);
    hyperram_b_write(res_row, (void *)residual_offset, row_bytes);

    for (int y = 1; y < height - 1; y++)
    {
        hyperram_b_read(rhs_curr, (void *)(level->rhs_offset + y * row_bytes), row_bytes);
        res_row[0] = 0.0f;
        res_row[width - 1] = 0.0f;

        for (int x = 1; x < width - 1; x++)
        {
            float lap = row_curr[x - 1] + row_curr[x + 1] + row_prev[x] + row_next[x] - 4.0f * row_curr[x];
            res_row[x] = rhs_curr[x] - lap;
        }

        hyperram_b_write(res_row, (void *)(residual_offset + y * row_bytes), row_bytes);

        if (y < height - 2)
        {
            memcpy(row_prev, row_curr, row_bytes);
            memcpy(row_curr, row_next, row_bytes);
            hyperram_b_read(row_next, (void *)(level->z_offset + (y + 2) * row_bytes), row_bytes);
        }
    }

    if (height > 1)
    {
        memset(res_row, 0, row_bytes);
        hyperram_b_write(res_row, (void *)(residual_offset + (height - 1) * row_bytes), row_bytes);
    }
}

static void mg_restrict_residual(const mg_level_t *fine, const mg_level_t *coarse, uint32_t residual_offset)
{
    const int fine_w = fine->width;
    const int fine_h = fine->height;
    const int coarse_w = coarse->width;
    const int coarse_h = coarse->height;
    const uint32_t fine_row_bytes = (uint32_t)fine_w * sizeof(float);
    const uint32_t coarse_row_bytes = (uint32_t)coarse_w * sizeof(float);

    float row_above[FRAME_WIDTH];
    float row_center[FRAME_WIDTH];
    float row_below[FRAME_WIDTH];
    float coarse_row[FRAME_WIDTH];

    if (fine_h == 0)
    {
        return;
    }

    for (int cy = 0; cy < coarse_h; cy++)
    {
        int fy = cy * 2;

        if (fy == 0)
        {
            hyperram_b_read(row_center, (void *)(residual_offset + fy * fine_row_bytes), fine_row_bytes);
            memcpy(row_above, row_center, fine_row_bytes);
        }
        else
        {
            hyperram_b_read(row_above, (void *)(residual_offset + (fy - 1) * fine_row_bytes), fine_row_bytes);
            hyperram_b_read(row_center, (void *)(residual_offset + fy * fine_row_bytes), fine_row_bytes);
        }

        if (fy + 1 < fine_h)
        {
            hyperram_b_read(row_below, (void *)(residual_offset + (fy + 1) * fine_row_bytes), fine_row_bytes);
        }
        else
        {
            memcpy(row_below, row_center, fine_row_bytes);
        }

        for (int cx = 0; cx < coarse_w; cx++)
        {
            int fx = cx * 2;
            float sum = row_center[fx] * 4.0f;

            if (fx > 0)
            {
                sum += row_center[fx - 1] * 2.0f;
            }
            if (fx < fine_w - 1)
            {
                sum += row_center[fx + 1] * 2.0f;
            }
            if (fy > 0)
            {
                sum += row_above[fx] * 2.0f;
            }
            if (fy < fine_h - 1)
            {
                sum += row_below[fx] * 2.0f;
            }
            if (fx > 0 && fy > 0)
            {
                sum += row_above[fx - 1];
            }
            if (fx < fine_w - 1 && fy > 0)
            {
                sum += row_above[fx + 1];
            }
            if (fx > 0 && fy < fine_h - 1)
            {
                sum += row_below[fx - 1];
            }
            if (fx < fine_w - 1 && fy < fine_h - 1)
            {
                sum += row_below[fx + 1];
            }

            coarse_row[cx] = sum / 16.0f;
        }

        hyperram_b_write(coarse_row, (void *)(coarse->rhs_offset + cy * coarse_row_bytes), coarse_row_bytes);
    }
}

static void mg_prolong_correction(const mg_level_t *coarse, const mg_level_t *fine)
{
    const int coarse_w = coarse->width;
    const int coarse_h = coarse->height;
    const int fine_w = fine->width;
    const int fine_h = fine->height;
    const uint32_t coarse_row_bytes = (uint32_t)coarse_w * sizeof(float);
    const uint32_t fine_row_bytes = (uint32_t)fine_w * sizeof(float);

    float coarse_row[FRAME_WIDTH];
    float fine_row_even[FRAME_WIDTH];
    float fine_row_odd[FRAME_WIDTH];

    for (int cy = 0; cy < coarse_h; cy++)
    {
        int fy = cy * 2;
        hyperram_b_read(coarse_row, (void *)(coarse->z_offset + cy * coarse_row_bytes), coarse_row_bytes);
        hyperram_b_read(fine_row_even, (void *)(fine->z_offset + fy * fine_row_bytes), fine_row_bytes);
        bool has_odd = (fy + 1) < fine_h;
        if (has_odd)
        {
            hyperram_b_read(fine_row_odd, (void *)(fine->z_offset + (fy + 1) * fine_row_bytes), fine_row_bytes);
        }

        for (int cx = 0; cx < coarse_w; cx++)
        {
            int fx = cx * 2;
            float val = coarse_row[cx];
            fine_row_even[fx] += val;
            if (fx + 1 < fine_w)
            {
                fine_row_even[fx + 1] += val * 0.5f;
            }
            if (has_odd)
            {
                fine_row_odd[fx] += val * 0.5f;
                if (fx + 1 < fine_w)
                {
                    fine_row_odd[fx + 1] += val * 0.25f;
                }
            }
        }

        hyperram_b_write(fine_row_even, (void *)(fine->z_offset + fy * fine_row_bytes), fine_row_bytes);
        if (has_odd)
        {
            hyperram_b_write(fine_row_odd, (void *)(fine->z_offset + (fy + 1) * fine_row_bytes), fine_row_bytes);
        }
    }
}

static void mg_vcycle(int level_index)
{
    const mg_level_t *level = &g_mg_levels[level_index];
    bool is_base_level = (level_index == g_mg_level_count - 1) || (level->width <= 40) || (level->height <= 30);

    if (is_base_level)
    {
        mg_gauss_seidel(level, coarse_iter);
        return;
    }

    mg_gauss_seidel(level, pre_smooth);

    mg_compute_residual(level, g_mg_residual_offset);
    const mg_level_t *coarse = &g_mg_levels[level_index + 1];
    mg_restrict_residual(level, coarse, g_mg_residual_offset);

    size_t coarse_bytes = (size_t)coarse->width * coarse->height * sizeof(float);
    mg_zero_buffer(coarse->z_offset, coarse_bytes);

    mg_vcycle(level_index + 1);

    mg_prolong_correction(coarse, level);
    mg_gauss_seidel(level, post_smooth);
}

static void mg_export_depth_map(const mg_level_t *level, float *z_min_out, float *z_max_out)
{
    const int width = level->width;
    const int height = level->height;
    const uint32_t row_bytes = (uint32_t)width * sizeof(float);

    float row_buffer[FRAME_WIDTH];
    uint8_t depth_row[FRAME_WIDTH];
    float z_min = 1e9f;
    float z_max = -1e9f;

    for (int y = 0; y < height; y++)
    {
        hyperram_b_read(row_buffer, (void *)(level->z_offset + y * row_bytes), row_bytes);
        for (int x = 0; x < width; x++)
        {
            if (row_buffer[x] < z_min)
            {
                z_min = row_buffer[x];
            }
            if (row_buffer[x] > z_max)
            {
                z_max = row_buffer[x];
            }
        }
    }

    float range = z_max - z_min;
    if (range < 1e-6f)
    {
        range = 1.0f;
    }

    for (int y = 0; y < height; y++)
    {
        hyperram_b_read(row_buffer, (void *)(level->z_offset + y * row_bytes), row_bytes);
        for (int x = 0; x < width; x++)
        {
            float normalized = (row_buffer[x] - z_min) / range;
            int val = (int)(normalized * 255.0f);
            if (val < 0)
            {
                val = 0;
            }
            if (val > 255)
            {
                val = 255;
            }
            depth_row[x] = (uint8_t)val;
        }
        hyperram_b_write(depth_row, (void *)(DEPTH_OFFSET + y * FRAME_WIDTH), FRAME_WIDTH);
    }

    if (z_min_out)
    {
        *z_min_out = z_min;
    }
    if (z_max_out)
    {
        *z_max_out = z_max;
    }
}

static void reconstruct_depth_multigrid(void)
{
    xprintf("[Thread3] Multigrid: Starting depth reconstruction\n");
    mg_prepare_layout();

    if (g_mg_level_count == 0)
    {
        xprintf("[Thread3] Multigrid: layout generation failed\n");
        return;
    }

    mg_zero_all_levels();

    const mg_level_t *fine_level = &g_mg_levels[0];
    mg_compute_divergence_to_hyperram(fine_level);

    for (int cycle = 0; cycle < mg_cycles; cycle++)
    {
        mg_vcycle(0);
    }

    float z_min = 0.0f;
    float z_max = 0.0f;
    mg_export_depth_map(fine_level, &z_min, &z_max);
    xprintf("[Thread3] Multigrid: Complete (range: %.2f - %.2f)\n", z_min, z_max);
}

#endif // USE_DEPTH_METHOD == 2

/* Main Thread3 entry function */
/* pvParameters contains TaskHandle_t */
void main_thread3_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    xprintf("[Thread3] Shape from Shading (p,q gradient) processor started\n");
    xprintf("[Thread3] Light source: ps=%.2f, qs=%.2f, ts=%.2f\n", LIGHT_PS, LIGHT_QS, LIGHT_TS);
#if USE_HELIUM_MVE
    xprintf("[Thread3] Helium MVE acceleration ENABLED\n");
#else
    xprintf("[Thread3] Helium MVE acceleration DISABLED (standard implementation)\n");
#endif

#if USE_DEPTH_METHOD == 2
    xprintf("[Thread3] Depth reconstruction: Multigrid (Poisson solver) - Medium quality, ~0.5-2sec/frame\n");
#else
    xprintf("[Thread3] Depth reconstruction: Simple (row integration) - Low quality, <1ms/frame\n");
#endif

    // スタック上の静的バッファ
    uint8_t yuv_lines[3][FRAME_WIDTH * 2]; // 3行分のYUV422データ
    uint8_t y_lines[3][FRAME_WIDTH];       // 3行分のY成分
    uint8_t edge_line[FRAME_WIDTH];        // エッジ検出結果（1行）
    uint8_t yuv_out[FRAME_WIDTH * 2];      // 出力用YUV422（1行）

    xprintf("[Thread3] Sobel buffers ready\n");

    // まず少し待機してシステムを安定させる
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1)
    {
        // フレーム全体を行単位で処理
        for (int y = 0; y < FRAME_HEIGHT; y++)
        {
            // 3行読み込み（前の行、現在の行、次の行）
            for (int row_offset = -1; row_offset <= 1; row_offset++)
            {
                int row_index = (row_offset + 1); // 0, 1, 2
                int read_y = y + row_offset;

                // 境界処理：範囲外は現在の行をコピー
                if (read_y < 0)
                    read_y = 0;
                if (read_y >= FRAME_HEIGHT)
                    read_y = FRAME_HEIGHT - 1;

                // HyperRAMから1行読み込み（640バイト = 320ピクセル×2）
                uint32_t offset = read_y * FRAME_WIDTH * 2;
                fsp_err_t err = hyperram_b_read(yuv_lines[row_index], (void *)offset, FRAME_WIDTH * 2);
                if (FSP_SUCCESS != err)
                {
                    xprintf("[Thread3] Read error at line %d: %d\n", read_y, err);
                    goto next_frame;
                }

                // Y成分を抽出
                extract_y_component(yuv_lines[row_index], y_lines[row_index], FRAME_WIDTH);
            }

            // p, q勾配を計算（Shape from Shading）
            // yuv_outに直接 [q0 p0 q1 p1 ...] 形式で640バイト書き込まれる
            compute_pq_gradients(y_lines[0], y_lines[1], y_lines[2], yuv_out);

            // HyperRAMのGRADIENT_OFFSETに書き込み（そのまま640バイト）
            uint32_t write_offset = GRADIENT_OFFSET + (y * FRAME_WIDTH * 2);
            fsp_err_t write_err = hyperram_b_write(yuv_out, (void *)write_offset, FRAME_WIDTH * 2);
            if (FSP_SUCCESS != write_err)
            {
                xprintf("[Thread3] Write error at line %d: %d\n", y, write_err);
                goto next_frame;
            }

#if USE_SIMPLE_DIRECT_P
            reconstruct_depth_simple_direct(write_offset, edge_line);
#else
            // 簡易深度マップ（行方向積分）
            reconstruct_depth_simple(yuv_out, edge_line);
#endif
            uint32_t depth_offset = DEPTH_OFFSET + (y * FRAME_WIDTH);
            fsp_err_t depth_write_err = hyperram_b_write(edge_line, (void *)depth_offset, FRAME_WIDTH);
            if (FSP_SUCCESS != depth_write_err)
            {
                xprintf("[Thread3] Depth write error at line %d: %d\n", y, depth_write_err);
                goto next_frame;
            }

            // 10行ごとにスケジューラに時間を与える
            if ((y % 10) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }

    next_frame:
#if USE_DEPTH_METHOD == 2
        // マルチグリッド深度マップ（ポアソン方程式反復解法）
        xprintf("[Thread3] Frame complete, starting Multigrid reconstruction\n");
        uint32_t mg_start = xTaskGetTickCount();
        reconstruct_depth_multigrid();
        uint32_t mg_end = xTaskGetTickCount();
        xprintf("[Thread3] Multigrid processing time: %u ms\n", (mg_end - mg_start));
        xprintf("[Thread3] Depth map ready for transmission\n");
        vTaskDelay(pdMS_TO_TICKS(200));
#else
        // 簡易版は行ごとに書き込み済み
        xprintf("[Thread3] Simple depth map complete\n");
        vTaskDelay(pdMS_TO_TICKS(200));
#endif
    }
}
