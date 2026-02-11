#include "cmsis_clang.h"
#include "common_data.h"
#include "main_thread0.h"
#include "hal_data.h"
#include "projdefs.h"
#include "r_ceu.h"
#include "putchar_ra8usb.h"
#include "cam.h"
#include "hyperram_integ.h"
#include "r_gpt.h"
#include "r_ioport.h"
#include "r_ospi_b.h"
#include <stddef.h>
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

/*
 * Camera capture pacing (ms).
 * NOTE: This is the dominant contributor to the perceived “shutter interval”.
 */
#ifndef CAMERA_CAPTURE_INTERVAL_MS
#define CAMERA_CAPTURE_INTERVAL_MS (100)
#endif

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

static void pwm_override_gtioc_pins(void)
{
    /* NOTE: ra_gen is auto-generated; if pin mux generation is not as expected,
     * override the GTIOC pin function at runtime using the IOPORT API. */
    fsp_err_t err;
    /* Some devices map GTIOC pins under a specific GPT PSEL group.
     * If one channel works and another doesn't, try the same PSEL group for all GTIOC pins. */
    uint32_t const cfg_gpt1 = (uint32_t)IOPORT_CFG_DRIVE_HIGH |
                              (uint32_t)IOPORT_CFG_PERIPHERAL_PIN |
                              (uint32_t)IOPORT_PERIPHERAL_GPT1;

    xprintf("[PWM/GPT] pin mux: forcing PSEL=IOPORT_PERIPHERAL_GPT0 for GTIOC pins\n");

    // P211,210は下記設定は行なわない。とまってしまう。
    // /* P211: GTIOC0A (GPT0 / A) */
    // err = g_ioport.p_api->pinCfg(&g_ioport_ctrl, BSP_IO_PORT_02_PIN_11, cfg_gpt0);
    // xprintf("[PWM/GPT] pin P211(GTIOC0A) cfg rc=%d\n", (int)err);

    // /* P210: GTIOC0B (GPT0 / B) */
    // err = g_ioport.p_api->pinCfg(&g_ioport_ctrl, BSP_IO_PORT_02_PIN_10, cfg_gpt0);
    // xprintf("[PWM/GPT] pin P210(GTIOC0B) cfg rc=%d\n", (int)err);

    /* P209: GTIOC1A (GPT1 / A)*/
    err = g_ioport.p_api->pinCfg(&g_ioport_ctrl, BSP_IO_PORT_02_PIN_09, cfg_gpt1);
    xprintf("[PWM/GPT] pin P209(GTIOC1A) cfg rc=%d\n", (int)err);

    /* P208: GTIOC1B (GPT1 / B) */
    err = g_ioport.p_api->pinCfg(&g_ioport_ctrl, BSP_IO_PORT_02_PIN_08, cfg_gpt1);
    xprintf("[PWM/GPT] pin P208(GTIOC1B) cfg rc=%d\n", (int)err);
}

void mypwm_init()
{

    //  init PWM(GPT)
    fsp_err_t err = FSP_SUCCESS;

    static timer_cfg_t s_timer0_pwm_cfg;
    static timer_cfg_t s_timer1_pwm_cfg;
    static bool s_pwm_cfg_ready = false;

    xprintf("[PWM/GPT] init begin\n");

    pwm_override_gtioc_pins();

    if (!s_pwm_cfg_ready)
    {
        s_timer0_pwm_cfg = g_timer0_cfg;
        s_timer0_pwm_cfg.mode = TIMER_MODE_PWM;
        s_timer0_pwm_cfg.p_context = NULL;

        s_timer1_pwm_cfg = g_timer1_cfg;
        s_timer1_pwm_cfg.mode = TIMER_MODE_PWM;
        s_timer1_pwm_cfg.p_context = NULL;

        s_pwm_cfg_ready = true;
    }

    xprintf("[PWM/GPT] CH0: module start\n");
    R_BSP_MODULE_START(FSP_IP_GPT, 0);

    xprintf("[PWM/GPT] CH0: open...\n");
    err = R_GPT_Open(&g_timer0_ctrl, &s_timer0_pwm_cfg);
    xprintf("[PWM/GPT] CH0: open rc=%d\n", (int)err);
    if (FSP_SUCCESS == err)
    {
        xprintf("[PWM/GPT] CH0 Open Ok.\n");

        err = R_GPT_OutputEnable(&g_timer0_ctrl, GPT_IO_PIN_GTIOCA);
        xprintf("[PWM/GPT] CH0: outA enable rc=%d\n", (int)err);
        err = R_GPT_OutputEnable(&g_timer0_ctrl, GPT_IO_PIN_GTIOCB);
        xprintf("[PWM/GPT] CH0: outB enable rc=%d\n", (int)err);

        err = R_GPT_DutyCycleSet(&g_timer0_ctrl, s_timer0_pwm_cfg.duty_cycle_counts, GPT_IO_PIN_GTIOCA);
        xprintf("[PWM/GPT] CH0: dutyA set rc=%d\n", (int)err);
        err = R_GPT_DutyCycleSet(&g_timer0_ctrl, s_timer0_pwm_cfg.duty_cycle_counts * 0, GPT_IO_PIN_GTIOCB);
        xprintf("[PWM/GPT] CH0: dutyB set rc=%d\n", (int)err);

        err = R_GPT_Start(&g_timer0_ctrl);
        xprintf("[PWM/GPT] CH0: start rc=%d\n", (int)err);
        if (FSP_SUCCESS == err)
        {
            xprintf("[PWM/GPT] CH0 START Okay.\n");
        }
    }
    xprintf("[PWM/GPT] CH0 init end\n");

    xprintf("[PWM/GPT] CH1: module start\n");
    R_BSP_MODULE_START(FSP_IP_GPT, 1);

    xprintf("[PWM/GPT] CH1: open...\n");
    err = R_GPT_Open(&g_timer1_ctrl, &s_timer1_pwm_cfg);
    xprintf("[PWM/GPT] CH1: open rc=%d\n", (int)err);
    if (FSP_SUCCESS == err)
    {
        xprintf("[PWM/GPT] CH1 Open Ok.\n");

        err = R_GPT_OutputEnable(&g_timer1_ctrl, GPT_IO_PIN_GTIOCA);
        xprintf("[PWM/GPT] CH1: outA enable rc=%d\n", (int)err);
        err = R_GPT_OutputEnable(&g_timer1_ctrl, GPT_IO_PIN_GTIOCB);
        xprintf("[PWM/GPT] CH1: outB enable rc=%d\n", (int)err);

        err = R_GPT_DutyCycleSet(&g_timer1_ctrl, s_timer1_pwm_cfg.duty_cycle_counts * 0, GPT_IO_PIN_GTIOCA);
        xprintf("[PWM/GPT] CH1: dutyA set rc=%d\n", (int)err);
        err = R_GPT_DutyCycleSet(&g_timer1_ctrl, s_timer1_pwm_cfg.duty_cycle_counts, GPT_IO_PIN_GTIOCB);
        xprintf("[PWM/GPT] CH1: dutyB set rc=%d\n", (int)err);

        err = R_GPT_Start(&g_timer1_ctrl);
        xprintf("[PWM/GPT] CH1: start rc=%d\n", (int)err);
        if (FSP_SUCCESS == err)
        {
            xprintf("[PWM/GPT] CH1 START Okay.\n");
        }

        timer_info_t p_info;
        err = R_GPT_InfoGet(&g_timer1_ctrl, &p_info);
        xprintf("[PWM/GPT] CH1: info rc=%d (clk=%lu, period=%lu)\n",
                (int)err,
                (unsigned long)p_info.clock_frequency,
                (unsigned long)p_info.period_counts);
    }

    xprintf("[PWM/GPT] init end\n");
    // uint32_t current_period_counts = p_info.period_counts;
    // /* Calculate the desired duty cycle based on the current period. Note that if the period could be larger than
    //  * UINT32_MAX / 100, this calculation could overflow. A cast to uint64_t is used to prevent this. The cast is
    //  * not required for 16-bit timers. */
    // uint32_t duty_cycle_counts =
    //     (uint32_t)(((uint64_t)current_period_counts * 50) /
    //                100);
    // /* Set the calculated duty cycle. */
    // R_GPT_OutputEnable(&g_timer1_ctrl, GPT_IO_PIN_GTIOCB);
    // fsp_err_t err = R_GPT_DutyCycleSet(&g_timer1_ctrl, duty_cycle_counts, GPT_IO_PIN_GTIOCB);
    // assert(FSP_SUCCESS == err);
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
    mypwm_init();
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
        vTaskDelay(pdMS_TO_TICKS(CAMERA_CAPTURE_INTERVAL_MS));
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
