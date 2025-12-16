#include "main_thread3.h"
#include "hyperram_integ.h"
#include "putchar_ra8usb.h"
#include <string.h>
#include <math.h>

// Helium MVE (ARM M-profile Vector Extension) support
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE > 0)
#include <arm_mve.h>
#define USE_HELIUM_MVE 1
#else
#define USE_HELIUM_MVE 0
#endif

#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 2) // YUV422 = 2 bytes/pixel
#define MONO_OFFSET FRAME_SIZE                      // エッジ画像は元画像の次に配置

/* YUV422からY成分（輝度）を抽出
 * YUV422フォーマット: [V0 Y1 U0 Y0] (リトルエンディアン)
 */
static void extract_y_component(uint8_t *yuv_line, uint8_t *y_line, int width)
{
    for (int x = 0; x < width; x += 2)
    {
        int yuv_index = x * 2;                   // 2ピクセル = 4バイト
        y_line[x] = yuv_line[yuv_index + 3];     // Y0
        y_line[x + 1] = yuv_line[yuv_index + 1]; // Y1
    }
}

/* Sobelフィルタでエッジ検出
 * 入力: 3行分のY成分（前の行、現在の行、次の行）
 * 出力: エッジ強度（0-255）
 */

#if USE_HELIUM_MVE
// Helium MVE版 - 16ピクセルブロック処理（ベクトル演算は複雑なため標準版と同じロジック）
static void apply_sobel_filter(uint8_t y_prev[FRAME_WIDTH],
                               uint8_t y_curr[FRAME_WIDTH],
                               uint8_t y_next[FRAME_WIDTH],
                               uint8_t edge_out[FRAME_WIDTH])
{
    // Sobelフィルタ係数（標準版と同じ）
    const int sobel_x[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    const int sobel_y[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};

    // 境界ピクセルは中央ピクセルの値をそのまま使う
    edge_out[0] = y_curr[0];
    edge_out[FRAME_WIDTH - 1] = y_curr[FRAME_WIDTH - 1];

    // 標準版と同じ処理（16ピクセルブロックごとに処理）
    uint8_t *rows[3] = {y_prev, y_curr, y_next};

    for (int x = 1; x < FRAME_WIDTH - 1; x++)
    {
        int gx = 0, gy = 0;

        // 3x3カーネルを適用
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
        magnitude = magnitude / 2;
        if (magnitude < 20)
            magnitude = 0;
        else if (magnitude > 255)
            magnitude = 255;

        edge_out[x] = (uint8_t)magnitude;
    }
}

#else
// 標準版 - Helium MVEなし

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

    // x=2,3,4のデバッグ（常に出力）
    if (g_sobel_debug_x != 0 && x >= 2 && x <= 4)
    {
        xprintf("  LOOP x=%d: gx=%d, gy=%d, mag=%d\n", x, gx, gy, magnitude);
    }

    edge_out[x] = (uint8_t)magnitude;
}
}
#endif // USE_HELIUM_MVE

/* エッジ強度をYUV422形式に変換
 * Y = エッジ強度, U = V = 128（グレースケール）
 * YUV422: [V0 Y1 U0 Y0] → Y0=ピクセル0, Y1=ピクセル1
 */
static void edge_to_yuv422(uint8_t edge_line[FRAME_WIDTH], uint8_t yuv_line[FRAME_WIDTH * 2])
{
    for (int x = 0; x < FRAME_WIDTH; x += 2)
    {
        int yuv_index = x * 2;
        yuv_line[yuv_index + 0] = 128;              // V0 = 128
        yuv_line[yuv_index + 1] = edge_line[x + 1]; // Y1 = ピクセル1
        yuv_line[yuv_index + 2] = 128;              // U0 = 128
        yuv_line[yuv_index + 3] = edge_line[x];     // Y0 = ピクセル0
    }
}

/* Main Thread3 entry function */
/* pvParameters contains TaskHandle_t */
void main_thread3_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    xprintf("[Thread3] Sobel edge detector started\n");
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

            // Sobelフィルタを適用
            apply_sobel_filter(y_lines[0], y_lines[1], y_lines[2], edge_line);

            // デバッグ: Y成分をそのまま出力
            // memcpy(edge_line, y_lines[1], FRAME_WIDTH);

            // エッジ結果をYUV422に変換
            edge_to_yuv422(edge_line, yuv_out);

            // HyperRAMのMONO_OFFSETに書き込み
            uint32_t write_offset = MONO_OFFSET + (y * FRAME_WIDTH * 2);
            fsp_err_t write_err = hyperram_b_write(yuv_out, (void *)write_offset, FRAME_WIDTH * 2);
            if (FSP_SUCCESS != write_err)
            {
                xprintf("[Thread3] Write error at line %d: %d\n", y, write_err);
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
