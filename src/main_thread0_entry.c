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

// #define VGA_WIDTH (256)
// #define VGA_HEIGHT (256)
// #define BYTE_PER_PIXEL (2)

#define RAM_DATA_LENGTH (64U * 1U) //
// void putchar_ra8usb(uint8_t c);

// ---- D-Cache を無効化（安全手順）----
static inline void dcache_disable_global(void)
{
    __DSB();
    __DMB();
    SCB_CleanDCache();      // ① ダーティラインを外へ書き戻す
    SCB_InvalidateDCache(); // ② すべて無効化（古い行を破棄）
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

// ---- I-Cache を無効化／有効化（必要な場合）----
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

// dst: メモリマップドHyperRAM先頭, src: 書き込み元, size: バイト数(2の倍数)
static inline fsp_err_t ospi_write_mmap(void *dst, const void *src, size_t size)
{
    fsp_err_t err;
    // 2バイト整列＆サイズ偶数を保証
    configASSERT(((uintptr_t)dst % 2) == 0);
    configASSERT((size % 2) == 0);

    // 書き込み前：順序＆キャッシュを外へ押し出す
    // 2) 送信元を必ずクリーン＋バリア（コントローラが読む側）
    __DSB();
    __DMB();

    // 分割コピー（必要に応じて）
    const size_t CHUNK = 64; // 32〜64Bあたりを推奨
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    size_t rem = size;
    while (rem)
    {
        size_t n = rem > CHUNK ? CHUNK : rem;
        n &= ~((size_t)1); // 偶数に丸める
        // memcpy(d, s, n);
        taskENTER_CRITICAL();
        err = R_OSPI_B_Write(&g_ospi0_ctrl, s, d, n);

        __DSB();
        __DMB();
        taskEXIT_CRITICAL();
        // if (FSP_SUCCESS != err)
        // {
        //     xprintf("[OSPI] HyperRAM write error!:%d\n", err);
        //     return err;
        // }
        // DMA完了待ち
        // while (ospi_b_dma_sent != true)
        // {
        //     vTaskDelay(pdMS_TO_TICKS(10));
        // }
        // ospi_b_dma_sent = false;

        d += n;
        s += n;
        rem -= n;
    }

    // 必要ならISB
    __ISB();
}

// 32Bラインに合わせて範囲を丸める（M85想定）
static inline void cache_inv_32B_aligned(const void *addr, size_t size)
{
    uintptr_t a = (uintptr_t)addr & ~(uintptr_t)31u;
    size_t n = (size + ((uintptr_t)addr - a) + 31u) & ~31u;
    SCB_InvalidateDCache_by_Addr((void *)a, n);
}

static inline bool ospi_verify_mmap_memcpy_chunked(const void *dst_dev, const void *src_golden, size_t size)
{
    configASSERT(((uintptr_t)dst_dev % 2) == 0);
    configASSERT((size % 2) == 0);

    const size_t CHUNK = RAM_DATA_LENGTH; // 256~1024あたりで調整
    const uint8_t *d = (const uint8_t *)dst_dev;
    const uint8_t *g = (const uint8_t *)src_golden;

    uint8_t *buf = (uint8_t *)pvPortMalloc(CHUNK);
    if (!buf)
        return false;

    size_t rem = size;
    while (rem)
    {
        size_t n = rem > CHUNK ? CHUNK : rem;

        __DSB();
        __DMB();
        cache_inv_32B_aligned(d, n); // 各チャンク前にInvalidate
        __DSB();
        __DMB();

        memcpy(buf, d, n);
        if (memcmp(buf, g, n) != 0)
        {
            vPortFree(buf);
            return false;
        }

        d += n;
        g += n;
        rem -= n;
    }

    vPortFree(buf);
    return true;
}

void main_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    //  init UART & printf
    xdev_out(putchar_ra8usb);
    xprintf("START\n");
    // init DVP camera

    cam_init(DEV_OV5642);
    xprintf("Camera Ready\n");
    // capture from camera
    vTaskDelay(pdMS_TO_TICKS(200));
    cam_capture();
    vTaskDelay(pdMS_TO_TICKS(200));
    cam_capture();
    cam_close();

    ospi_b_dma_sent = false;
    xprintf("!srt\n");
    // cast pointer
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

    R_XSPI0->CMCFGCS[1].CMCFG0_b.WPBSTMD = 0; // 0:WRAP, 1:LINEAR

    uint32_t cmg = R_XSPI0->CMCFGCS[1].CMCFG0;
    uint32_t wp = (cmg & R_XSPI0_CMCFGCS_CMCFG0_WPBSTMD_Msk) >> R_XSPI0_CMCFGCS_CMCFG0_WPBSTMD_Pos;

    xprintf("-------------------WPBSTMD = 0x%08X-----------------------\n", wp);

    // dcache_disable_global();
    // icache_disable_global();

    ////////////////////////////
    // write to HyperRAM
    err = hyperram_b_write(image_p8, 0x00, VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] HyperRAM write error!\n");
    }

    // ospi_write_mmap(hyperram_ptr, image_p8, VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL);
    ////////////////////////////

    xprintf("[OSPI] write end\n");
    hyperram_ptr = HYPERRAM_BASE_ADDR;
    hyperram_ptr32 = HYPERRAM_BASE_ADDR;
    image_p32 = (uint32_t *)g_image_qvga_sram;

    for (uint32_t z = 0; z < VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL / 4; z++)
    {
        uint32_t adr = z * 4;
        err = ospi_raw_trans(&g_ospi0_trans,
                             OSPI_B_COMMAND_READ, 1,
                             adr, 4,
                             0, 4,
                             8, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
        if (FSP_SUCCESS != err)
        {
            xprintf("[OSPI] direct transfer error!\n");
        }
        xprintf("0x%08X\n", g_ospi0_trans.data);
    }

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL; i += RAM_DATA_LENGTH)
    {
        bool cm;
        // 読み戻し直前：
        cm = memcmp(image_p8, hyperram_ptr, RAM_DATA_LENGTH);
        // cm = ospi_verify_mmap(hyperram_ptr, image_p8, RAM_DATA_LENGTH);
        SCB_CleanDCache_by_Addr((uint32_t *)hyperram_ptr, RAM_DATA_LENGTH);
        // cm = ospi_verify_mmap_memcpy_chunked(hyperram_ptr, image_p8, RAM_DATA_LENGTH);

        // if (ospi_verify_mmap(hyperram_ptr, image_p8, RAM_DATA_LENGTH) == false)
        if (cm != 0)
        {
            xprintf("[OSPI] HyperRAM verify error!\n");
        }
        else
        {
            xprintf("[OSPI] HyperRAM verify OK.\n");
        }
        vTaskDelay(pdMS_TO_TICKS(1));
        hyperram_ptr += RAM_DATA_LENGTH;
        image_p8 += RAM_DATA_LENGTH;
    }

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
