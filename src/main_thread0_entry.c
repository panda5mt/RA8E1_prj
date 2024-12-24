#include "main_thread0.h"
#include "r_gpt.h"
#include "r_sci_b_uart.h"
#include "xprintf_helper.h"

/* Main Thread entry function */
/* pvParameters contains TaskHandle_t */
void main_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);
    R_BSP_MODULE_START(FSP_IP_SCI, 9);
    R_SCI_B_UART_Open(&g_uart9_ctrl, &g_uart9_cfg);

    xdev_out(put_char_ra8);
    //  init UART & printf

    R_BSP_MODULE_START(FSP_IP_GPT, 3);
    //  init PWM
    if (FSP_SUCCESS == R_GPT_Open(&g_timer3_ctrl, &g_timer3_cfg))
    {
        xprintf("GPT Open OK!\n");
    }
    if (FSP_SUCCESS == R_GPT_Start(&g_timer3_ctrl))
    {
        xprintf("GPT Start OK!\n");
    }

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
