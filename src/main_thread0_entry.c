#include "cmsis_clang.h"
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

#define RAM_DATA_LENGTH (64U * 1U)
// void putchar_ra8usb(uint8_t c);
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
        err = R_OSPI_B_Write(&g_ospi0_ctrl, s, d, n);
        // 書いた直後
        SCB_CleanDCache_by_Addr(dst, n);
        __DSB();
        __DMB();

        if (FSP_SUCCESS != err)
        {
            xprintf("[OSPI] HyperRAM write error!:%d\n", err);
            return err;
        }
        // DMA完了待ち
        while (ospi_b_dma_sent != true)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        ospi_b_dma_sent = false;

        //  各チャンクを確実に外へ
        SCB_CleanDCache_by_Addr((uint32_t *)d, n);
        __DSB();
        __DMB();
        d += n;
        s += n;
        rem -= n;
    }

    // 必要ならISB
    __ISB();
}

static inline bool ospi_verify_mmap(const void *dst, const void *src, size_t size)
{
    configASSERT(((uintptr_t)dst % 2) == 0);
    configASSERT((size % 2) == 0);

    // 読み戻し前：必ずキャッシュ無効化
    __DSB();
    __DMB();

    const uint16_t *a = (const uint16_t *)dst;
    const uint16_t *b = (const uint16_t *)src;
    // 読み戻し直前：
    SCB_InvalidateDCache_by_Addr(dst, size);

    for (size_t i = 0; i < size / 2; ++i)
    {
        if (a[i] != b[i])
            return false;
    }
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
                             OSPI_B_COMMAND_READ, 2,
                             adr, 4,
                             0, 4,
                             16, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
        if (FSP_SUCCESS != err)
        {
            xprintf("[OSPI] direct transfer error!\n");
        }
        xprintf("0x%08X\n", g_ospi0_trans.data);
    }

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL; i += RAM_DATA_LENGTH)
    {
        // if (memcmp(image_p8, hyperram_ptr, RAM_DATA_LENGTH) != 0)
        if (ospi_verify_mmap(hyperram_ptr, image_p8, RAM_DATA_LENGTH) == false)
        {
            xprintf("[OSPI] HyperRAM verify error!\n");
        }
        else
        {
            xprintf("[OSPI] HyperRAM verify OK!\n");
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
