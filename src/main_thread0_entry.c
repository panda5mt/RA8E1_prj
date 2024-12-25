#include "main_thread0.h"
#include "hal_data.h"
#include "r_ceu.h"
#include "xprintf_helper.h"
// #include "sccb_if.h"

static volatile bool i2c_writing = false;

void g_ceu0_user_callback(capture_callback_args_t *p_args)
{
    if (CEU_EVENT_FRAME_END == p_args->event)
    {
        /// g_capture_ready = true;
    }
}

void g_i2c_callback(i2c_master_callback_args_t *p_args)
{
    switch (p_args->event)
    {
    case I2C_MASTER_EVENT_TX_COMPLETE:
        xprintf("i2c complete\n");
        i2c_writing = false;
        break;
    default:
        xprintf("i2c ongoing\n");
        break;
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
    timer_info_t p_info;
    R_GPT_InfoGet(&g_timer3_ctrl, &p_info);
    xprintf("PWM = %d[kHz]\n", (p_info.clock_frequency / p_info.period_counts) / 1000);

    // init I2C
    // sccb_init(DEV_OV5642);

    R_BSP_MODULE_START(FSP_IP_IIC, 1);
    if (FSP_SUCCESS == R_IIC_MASTER_Open(&g_i2c_master1_ctrl, &g_i2c_master1_cfg))
    {
        xprintf("SCCB(I2C) master Init OK\n");
    }
    uint8_t sccb_dat[3];

    sccb_dat[0] = 0x31;
    sccb_dat[1] = 0x03;
    sccb_dat[2] = 0x93;

    // Write data to register(s) over I2C
    if (FSP_SUCCESS == R_IIC_MASTER_Write(&g_i2c_master1_ctrl, sccb_dat, 3, false))
    {
        xprintf("data sent OK\n");
    }
    else
    {
        xprintf("data sent error!\n");
    }

    // init camera
    if (FSP_SUCCESS == R_CEU_Open(&g_ceu0_ctrl, &g_ceu0_cfg))
    {
        xprintf("Camera CEU Init OK\n");
    }

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
