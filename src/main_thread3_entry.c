#include "main_thread3.h"
#include "hyperram_integ.h"
#include "putchar_ra8usb.h"
#include <string.h>
#include <math.h>
#include "arm_math.h" // CMSIS-DSP (FFT関数)

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
 * 本格版（FFT）は後で実装
 */
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

            // 深度復元（簡易版：p勾配の積分）
            // edge_lineバッファを再利用して深度マップを生成
            reconstruct_depth_simple(yuv_out, edge_line);

            // HyperRAMのDEPTH_OFFSETに書き込み（320バイト/行）
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
        // 処理完了（2秒待機して次のサイクル）
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
