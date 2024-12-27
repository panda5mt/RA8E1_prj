#include "main_thread0.h"
#include "hal_data.h"
#include "r_ceu.h"
#include "xprintf_helper.h"
#include "sccb_if.h"

bool g_ceu_capture_error;
bool g_ceu_capture_complete;

void g_ceu0_user_callback(capture_callback_args_t *p_args)
{
    /* Multiple event flags may be set simultaneously */
    if (p_args->event & (uint32_t) ~(CEU_EVENT_HD | CEU_EVENT_VD | CEU_EVENT_FRAME_END))
    {
        /* Error processing should occur first. Application should not process complete event if error occurred. */
        g_ceu_capture_error = true;
    }
    else
    {
        if (p_args->event & CEU_EVENT_VD)
        {
            /* Capture has started. Process V-Sync event. */
        }
        if (p_args->event & CEU_EVENT_FRAME_END)
        {
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

    R_CEU_Open(&g_ceu0_ctrl, &g_ceu0_cfg);

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
