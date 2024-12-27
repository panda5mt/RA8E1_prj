#include "main_thread0.h"
#include "hal_data.h"
#include "r_ceu.h"
#include "xprintf_helper.h"
#include "sccb_if.h"

#define VGA_WIDTH (256)
#define VGA_HEIGHT (256)
#define BYTE_PER_PIXEL (2)

bool g_ceu_capture_error;
bool g_ceu_capture_complete;
int32_t g_flag1;
uint8_t g_image_qvga_sram[VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL] /*BSP_ALIGN_VARIABLE(8)*/;
void g_ceu0_user_callback(capture_callback_args_t *p_args)
{
    g_flag1 = (uint32_t)p_args->event;
    /* Multiple event flags may be set simultaneously */
    if (p_args->event & (uint32_t) ~(CEU_EVENT_HD | CEU_EVENT_VD | CEU_EVENT_FRAME_END))
    {
        /* Error processing should occur first. Application should not process complete event if error occurred. */
        g_ceu_capture_error = true;
        g_flag1 = 3;
    }
    else
    {
        if (p_args->event & CEU_EVENT_VD)
        {
            g_flag1 = 1;
            /* Capture has started. Process V-Sync event. */
        }
        if (p_args->event & CEU_EVENT_FRAME_END)
        {
            g_flag1 = 2;
            /* Capture is complete and no error has occurred */
            g_ceu_capture_complete = true;
        }
    }
}

/* Main Thread entry function */
/* pvParameters contains TaskHandle_t */
void main_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    //  init UART & printf
    xdev_out(put_char_ra8);

    // init I2C and PWM
    sccb_and_clk_init();

    ////////////////////// CAMERA START
    fsp_err_t err = FSP_SUCCESS;
    g_flag1 = 0;
    R_CEU_Open(&g_ceu0_ctrl, &g_ceu0_cfg);
    g_ceu_capture_error = false;
    g_ceu_capture_complete = false;

    // R_BSP_MODULE_START(FSP_IP_CEC, 0);
    err = R_CEU_CaptureStart(&g_ceu0_ctrl, g_image_qvga_sram);
    assert(FSP_SUCCESS == err);
    xprintf("[Camera Capture] Start.\n");

    while (!g_ceu_capture_complete /*&& !g_ceu_capture_error*/)
    {
        if (g_flag1 != 0)
            xprintf("g_flag = %d\n", g_flag1);
        /* Wait for capture to complete. */
        if (g_ceu_capture_error)
        {
            g_ceu_capture_error = false;
        }
    }

    xprintf("[Camera Capture] end\n");
    /* Process image here if capture was successful. */
    err = R_CEU_Close(&g_ceu0_ctrl);
    assert(FSP_SUCCESS == err);
    ////////////////////// CAMERA END

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
