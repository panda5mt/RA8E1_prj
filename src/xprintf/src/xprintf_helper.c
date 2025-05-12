#include "bsp_api.h"

#include "hal_data.h"
#include "xprintf_helper.h"

bool first_call = true;
int g_err_flag = 0, g_tx_flag = 0, g_rx_flag = 0;

void put_char_ra8(uint8_t ch)
{

    // R_SCI_B_UART_Writeが1文字送信が難しそうなので対策(fsp v.5.7.0)
    uint8_t p[2];
    p[0] = ch;
    p[1] = '\0';

    if (first_call == true)
    {
        R_BSP_MODULE_START(FSP_IP_SCI, 9);
        // init UART & printf
        R_SCI_B_UART_Open(&g_uart9_ctrl, &g_uart9_cfg);
        first_call = false;
    }

    uint32_t len = (uint32_t)(sizeof(p) / sizeof(uint8_t));

    R_SCI_B_UART_Write(&g_uart9_ctrl,
                       p,
                       (len == 1) ? len += 1 : len);
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