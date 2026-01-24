#include "cmsis_clang.h"
#include "common_data.h"
#include "main_thread0.h"
#include "hal_data.h"
#include "projdefs.h"
#include "r_ceu.h"
#include "putchar_ra8usb.h"
#include "cam.h"
#include "hyperram_integ.h"
#include "r_ospi_b.h"
#include <arm_acle.h>
#include "video_frame_buffer.h"
#include "verify_mode.h"

/* Published HyperRAM base offset for the most recently written camera frame. */
volatile uint32_t g_video_frame_base_offset = (uint32_t)VIDEO_FRAME_BASE_OFFSET_DEFAULT;

/* Published monotonic sequence for the most recently written camera frame. */
volatile uint32_t g_video_frame_seq = 0;

// #define VGA_WIDTH (256)
// #define VGA_HEIGHT (256)
// #define BYTE_PER_PIXEL (2)

#define RAM_DATA_LENGTH (64U) //
// void putchar_ra8usb(uint8_t c);

// ---- D-Cache を無効化(安全手順)----
static inline void dcache_disable_global(void)
{
    __DSB();
    __DMB();
    SCB_CleanDCache();      // ① ダーティラインを外へ書き戻す
    SCB_InvalidateDCache(); // ② すべて無効化(古い行を破棄)
    SCB_DisableDCache();    // ③ D-CacheをOFF
    __DSB();
    __ISB();
}

// ---- D-Cache を有効化 ----
static inline void dcache_enable_global(void)
{
    __DSB();
    __DMB();
    SCB_InvalidateDCache(); // 有効化前に中身を空に
    SCB_EnableDCache();     // ON
    __DSB();
    __ISB();
}

// ---- I-Cache を無効化／有効化(必要な場合)----
static inline void icache_disable_global(void)
{
    __DSB();
    __DMB();
    SCB_InvalidateICache(); // 中身を空に
    SCB_DisableICache();    // OFF
    __DSB();
    __ISB();
}

static inline void icache_enable_global(void)
{
    __DSB();
    __DMB();
    SCB_InvalidateICache();
    SCB_EnableICache(); // ON
    __DSB();
    __ISB();
}

/* Main Thread entry function */
/* pvParameters contains TaskHandle_t */

void main_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    //  init UART & printf
    xdev_out(putchar_ra8usb);
    xprintf("START\n");

#if APP_MODE_FFT_VERIFY && APP_MODE_FFT_VERIFY_DISABLE_CAMERA
    xprintf("[Thread0] APP_MODE_FFT_VERIFY=1: camera disabled\n");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
    // init DVP camera

    cam_init(DEV_OV5642);
    xprintf("Camera Ready\n");
    // capture from camera
    vTaskDelay(pdMS_TO_TICKS(200));
    cam_capture();
    // cam_close();

    ospi_b_dma_sent = false;
    // xprintf("!srt\n");
    //  cast pointer
    uint8_t *image_p8 = (uint8_t *)g_image_qvga_sram;
    uint32_t *image_p32 = (uint32_t *)g_image_qvga_sram;
    uint8_t *hyperram_ptr = (uint8_t *)HYPERRAM_BASE_ADDR;
    uint32_t *hyperram_ptr32 = (uint32_t *)HYPERRAM_BASE_ADDR;

    fsp_err_t err = FSP_SUCCESS;
    err = hyperram_init();
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] HyperRAM init error!\n");
        return;
    }

    // icache_enable_global();
    dcache_disable_global();

    uint32_t next_write_base = video_frame_align_u32((uint32_t)VIDEO_FRAME_BASE_OFFSET_DEFAULT);

    while (1)
    {
        // カメラキャプチャ実行
        cam_capture();

        // HyperRAMに書き込み(動画ストリーミング用)
        const uint32_t frame_bytes = (uint32_t)(VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL);

        err = hyperram_b_write(image_p8, (void *)next_write_base, frame_bytes);
        if (FSP_SUCCESS != err)
        {
            xprintf("[OSPI] HyperRAM write error!\n");
        }
        else
        {
            /* Publish the base where this frame now lives. */
            g_video_frame_base_offset = next_write_base;

            /* Publish frame completion (lets consumers avoid mid-write reads). */
            g_video_frame_seq++;

            /* Advance the write base for the next frame (optional). */
            next_write_base = video_frame_next_base_u32(next_write_base, frame_bytes);
        }

        // フレーム間隔：動画ストリーミングのフレームレートに合わせる
        // 500msに変更してHyperRAM競合を軽減
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ////////////////////////////
    // xprintf("[OSPI] write end\n");
    // hyperram_ptr = HYPERRAM_BASE_ADDR;
    // hyperram_ptr32 = HYPERRAM_BASE_ADDR;
    // image_p32 = (uint32_t *)g_image_qvga_sram;

    // for (uint32_t z = 0; z < VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL / 4; z++)
    // {
    //     uint32_t adr = z * 4;
    //     adr = ((adr & 0xfffffff0) << 6) | (adr & 0x0f); // Octal ram address format
    //     xprintf("0x%08X\n", *((volatile uint32_t *)((uint8_t *)HYPERRAM_BASE_ADDR + adr)));
    // }

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
