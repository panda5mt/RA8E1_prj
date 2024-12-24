#include "main_thread0.h"
#include "hal_data.h"
#include "r_ceu.h"
#include "xprintf_helper.h"

void g_ceu0_user_callback(capture_callback_args_t *p_args)
{
    if (CEU_EVENT_FRAME_END == p_args->event)
    {
        /// g_capture_ready = true;
    }
}

/* Main Thread entry function */
/* pvParameters contains TaskHandle_t */
void main_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    //  init UART & printf
    xdev_out(put_char_ra8);

    //  init PWM(GPT)
    R_BSP_MODULE_START(FSP_IP_GPT, 3);
    if (FSP_SUCCESS == R_GPT_Open(&g_timer3_ctrl, &g_timer3_cfg))
    {
        xprintf("GPT Open OK!\n");
    }
    if (FSP_SUCCESS == R_GPT_Start(&g_timer3_ctrl))
    {
        xprintf("GPT Start OK!\n");
    }
    R_CEU_Open(&g_ceu0_ctrl, &g_ceu0_cfg);

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
