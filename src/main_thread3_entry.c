#include "main_thread3.h"
#include "hyperram_integ.h"
#include "putchar_ra8usb.h"
#include <string.h>

#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 2) // YUV422 = 2 bytes/pixel
#define MONO_OFFSET FRAME_SIZE                      // モノクロ画像は元画像の次に配置
#define BATCH_SIZE 256                              // バッチ処理サイズ（256バイト）

/* YUV422データをモノクロ化する関数
 * YUV422フォーマット: [V0 Y1 U0 Y0] (リトルエンディアン, 4バイト/2ピクセル)
 * モノクロ化: Y成分は保持、U=128, V=128（無彩色）
 */
static void convert_to_monochrome(uint8_t *src, uint8_t *dest, uint32_t size)
{
    // YUV422は4バイトで2ピクセル: [V0 Y1 U0 Y0]
    for (uint32_t i = 0; i < size; i += 4)
    {
        uint8_t y0 = src[i + 3]; // Y0
        uint8_t y1 = src[i + 1]; // Y1

        // モノクロYUV422: Y成分保持、U/V=128（無彩色グレー）
        dest[i + 0] = 128; // V0 = 128
        dest[i + 1] = y1;  // Y1保持
        dest[i + 2] = 128; // U0 = 128
        dest[i + 3] = y0;  // Y0保持
    }
}

/* Main Thread3 entry function */
/* pvParameters contains TaskHandle_t */
void main_thread3_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    xprintf("[Thread3] Monochrome converter started\n");

    // スタック上の静的バッファを使用（ヒープ不要）
    uint8_t buffer[BATCH_SIZE];
    uint8_t mono_buffer[BATCH_SIZE];

    xprintf("[Thread3] Static buffers ready (%d bytes each)\n", BATCH_SIZE);

    // まず少し待機してシステムを安定させる
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1)
    {
        // フレーム全体をバッチ処理
        for (uint32_t offset = 0; offset < FRAME_SIZE; offset += BATCH_SIZE)
        {
            uint32_t read_size = (offset + BATCH_SIZE <= FRAME_SIZE) ? BATCH_SIZE : (FRAME_SIZE - offset);

            // 1. HyperRAMから256バイト読み込み
            fsp_err_t err = hyperram_b_read(buffer, (void *)offset, read_size);
            if (FSP_SUCCESS != err)
            {
                xprintf("[Thread3] Read error at %u: %d\n", offset, err);
                break;
            }

            // 2. モノクロ変換
            convert_to_monochrome(buffer, mono_buffer, read_size);

            // 3. HyperRAMの次の番地（MONO_OFFSET + offset）に書き込み
            err = hyperram_b_write(mono_buffer, (void *)(MONO_OFFSET + offset), read_size);
            if (FSP_SUCCESS != err)
            {
                xprintf("[Thread3] Write error at %u: %d\n", offset, err);
                break;
            }

            // バッチごとに少し待機してThread1に処理を譲る
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        // 処理完了（10秒待機して次のサイクル）
        xprintf("[Thread3] Frame conversion complete\n");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
