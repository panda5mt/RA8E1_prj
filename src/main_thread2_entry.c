#include "main_thread2.h"
/* Main Thread2 entry function */
/* pvParameters contains TaskHandle_t */

int g_tx_flag, g_rx_flag, g_err_flag;
uint8_t g_tx_buf[2];
uint8_t ucHeap[configTOTAL_HEAP_SIZE];

void main_thread2_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* TODO: add your own code here */
    while (1)
    {
        vTaskDelay(1);

        g_tx_buf[0] = 'A';
        g_tx_buf[1] = '\0';
        R_BSP_MODULE_START(FSP_IP_USBFS, 0);
        fsp_err_t err = FSP_SUCCESS;
        err = RM_COMMS_USB_PCDC_Open(&g_comms_usb_pcdc0_ctrl, &g_comms_usb_pcdc0_cfg);
        if (FSP_SUCCESS != err)
        {
            /* Handle any errors. */
        }
        while (true)
        {
            /* Send data. */
            g_err_flag = 0;
            g_tx_flag = 0;
            err = RM_COMMS_USB_PCDC_Write(&g_comms_usb_pcdc0_ctrl, g_tx_buf, 2);
            if (FSP_SUCCESS != err)
            {
                /* Handle any errors. */
            }
            while ((0 == g_tx_flag) && (0 == g_err_flag))
            {
                /* Wait callback */
            }
            /* Receive data. */
            // g_err_flag = 0;
            // g_rx_flag = 0;
            // err = RM_COMMS_USB_PCDC_Read(&g_comms_usb_pcdc0_ctrl, g_rx_buf, RX_BUF_LEN);
            // if (FSP_SUCCESS != err)
            // {
            //     /* Handle any errors.*/
            // }
            // while ((0 == g_rx_flag) && (0 == g_err_flag))
            // {
            //     /* Wait callback */
            // }
        }
    }
}
void rm_comms_usb_pcdc_callback(rm_comms_callback_args_t *p_args)
{
    if (p_args->event == RM_COMMS_EVENT_TX_OPERATION_COMPLETE)
    {
        g_tx_flag = 1;
    }
    else if (p_args->event == RM_COMMS_EVENT_RX_OPERATION_COMPLETE)
    {
        g_rx_flag = 1;
    }
    else
    {
        g_err_flag = 1;
    }
}