#include "main_thread0.h"
#include "hal_data.h"
#include "r_ceu.h"
#include "xprintf.h"
#include "cam.h"

// #define VGA_WIDTH (256)
// #define VGA_HEIGHT (256)
// #define BYTE_PER_PIXEL (2)

bool first_call = true;
int g_err_flag = 0, g_tx_flag = 0, g_rx_flag = 0;

// void put_char_ra8_usb(uint8_t ch);
/* Main Thread entry function */
/* pvParameters contains TaskHandle_t */

void main_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    //  init UART & printf
    // xdev_out(put_char_ra8_usb);

    // init DVP camera
    cam_init(DEV_OV3640);
    // capture from camera
    vTaskDelay(pdMS_TO_TICKS(200));
    cam_capture();
    vTaskDelay(pdMS_TO_TICKS(200));
    cam_capture();
    cam_close();
    xprintf("!srt\n");
    // cast pointer
    uint32_t *image_p32 = (uint32_t *)g_image_qvga_sram;

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL / 4; i++)
    {

        xprintf("0x%08X\n",
                image_p32[i]);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// void put_char_ra8_usb(uint8_t ch)
// {
//     fsp_err_t err = FSP_SUCCESS;
//     // R_SCI_B_UART_Writeが1文字送信が難しそうなので対策(fsp v.5.7.0)
//     uint8_t p[2];
//     p[0] = ch;
//     p[1] = '\0';

//     if (first_call == true)
//     {
//         err = RM_COMMS_USB_PCDC_Open(&g_comms_usb_pcdc0_ctrl, &g_comms_usb_pcdc0_cfg);
//         first_call = false;
//     }

//     if (FSP_SUCCESS != err)
//     {
//         /* Handle any errors. */
//     }

//     /* Send data. */
//     g_err_flag = 0;
//     g_tx_flag = 0;
//     err = RM_COMMS_USB_PCDC_Write(&g_comms_usb_pcdc0_ctrl, p, 1);
//     if (FSP_SUCCESS != err)
//     {
//         /* Handle any errors. */
//     }
//     while ((0 == g_tx_flag) && (0 == g_err_flag))
//     {
//         /* Wait callback */
//     }
//     // /* Receive data. */
//     // g_err_flag = 0;
//     // g_rx_flag = 0;
//     // err = RM_COMMS_USB_PCDC_Read(&g_comms_usb_pcdc_ctrl, g_rx_buf, RX_BUF_LEN);
//     // if (FSP_SUCCESS != err)
//     // {
//     //     /* Handle any errors.*/
//     // }
//     // while ((0 == g_rx_flag) && (0 == g_err_flag))
//     // {
//     //     /* Wait callback */
//     // }
// }

// void rm_comms_usb_pcdc_callback(rm_comms_callback_args_t *p_args)
// {
//     if (p_args->event == RM_COMMS_EVENT_TX_OPERATION_COMPLETE)
//     {
//         g_tx_flag = 1;
//     }
//     else if (p_args->event == RM_COMMS_EVENT_RX_OPERATION_COMPLETE)
//     {
//         g_rx_flag = 1;
//     }
//     else
//     {
//         g_err_flag = 1;
//     }
// }