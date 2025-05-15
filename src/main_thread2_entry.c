#include "main_thread2.h"
#include "projdefs.h"

/* New Thread entry function */
// uint8_t ucHeap[configTOTAL_HEAP_SIZE];

// uint32_t g_usb_read_data;

// /* 115200 8n1 by default */
// static usb_pcdc_linecoding_t g_line_coding = {
//     .dw_dte_rate = 115200,
//     .b_char_format = STOP_BITS_1,
//     .b_parity_type = PARITY_NONE,
//     .b_data_bits = DATA_BITS_8,
// };

// static volatile usb_pcdc_ctrllinestate_t g_control_line_state = {
//     .bdtr = 0,
//     .brts = 0,
// };

// void usb_cdc_rtos_callback(usb_event_info_t *event, usb_hdl_t handle, usb_onoff_t onoff)
// {
//     FSP_PARAMETER_NOT_USED(handle);
//     FSP_PARAMETER_NOT_USED(onoff);

//     usb_setup_t setup;

//     switch (event->event)
//     {
//     case USB_STATUS_CONFIGURED:
//         break;
//     case USB_STATUS_WRITE_COMPLETE:
//         if (pdTRUE == xSemaphoreGiveFromISR(g_usb_write_complete_binary_semaphore, NULL))
//         {
//             __NOP();
//         }
//         break;
//     case USB_STATUS_READ_COMPLETE:
//         if (pdTRUE == xQueueSendFromISR(g_usb_read_queue, &event->data_size, NULL))
//         {
//             __NOP();
//         }

//         break;
//     case USB_STATUS_REQUEST: /* Receive Class Request */
//         R_USB_SetupGet(event, &setup);
//         if (USB_PCDC_SET_LINE_CODING == (setup.request_type & USB_BREQUEST))
//         {
//             R_USB_PeriControlDataGet(event, (uint8_t *)&g_line_coding, LINE_CODING_LENGTH);
//         }
//         else if (USB_PCDC_GET_LINE_CODING == (setup.request_type & USB_BREQUEST))
//         {
//             R_USB_PeriControlDataSet(event, (uint8_t *)&g_line_coding, LINE_CODING_LENGTH);
//         }
//         else if (USB_PCDC_SET_CONTROL_LINE_STATE == (event->setup.request_type & USB_BREQUEST))
//         {
//             fsp_err_t err = R_USB_PeriControlDataGet(event, (uint8_t *)&g_control_line_state, sizeof(g_control_line_state));
//             if (FSP_SUCCESS == err)
//             {
//                 g_control_line_state.bdtr = (unsigned char)((event->setup.request_value >> 0) & 0x01);
//                 g_control_line_state.brts = (unsigned char)((event->setup.request_value >> 1) & 0x01);
//             }
//         }
//         else
//         {
//             /* none */
//         }
//         break;
//     case USB_STATUS_REQUEST_COMPLETE:
//         __NOP();
//         break;
//     case USB_STATUS_SUSPEND:
//     case USB_STATUS_DETACH:
//     case USB_STATUS_DEFAULT:
//         __NOP();
//         break;
//     default:
//         __NOP();
//         break;
//     }
// }
#define TX_BUF_LEN (64)
#define RX_BUF_LEN (64)

int g_err_flag, g_tx_flag, g_rx_flag;
uint8_t g_tx_buf[TX_BUF_LEN];
uint8_t g_rx_buf[RX_BUF_LEN];

/* pvParameters contains TaskHandle_t */
void main_thread2_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    R_BSP_MODULE_START(FSP_IP_GPT, 5);
    if (FSP_SUCCESS == R_GPT_Open(&g_timer5_ctrl, &g_timer5_cfg))
    {
        // xprintf("[PWM/GPT] Open Ok.\n");
    }
    if (FSP_SUCCESS == R_GPT_Start(&g_timer5_ctrl))
    {
        // xprintf("[PWM/GPT] Start Ok.\n");
    }
    timer_info_t p_info5;
    R_GPT_InfoGet(&g_timer5_ctrl, &p_info5);
    // xprintf("[PWM/GPT] %d[Hz]\n", (p_info5.clock_frequency / p_info5.period_counts));

    // PWM周期の把握
    timer_info_t p_info05;
    R_GPT_InfoGet(&g_timer5_ctrl, &p_info05);
    int per = p_info05.period_counts;

    fsp_err_t err = FSP_SUCCESS;
    err = RM_COMMS_USB_PCDC_Open(&g_comms_usb_pcdc0_ctrl, &g_comms_usb_pcdc0_cfg);
    if (FSP_SUCCESS != err)
    {
        /* Handle any errors. */
    }
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
        /* Send data. */

        g_err_flag = 0;
        g_tx_flag = 0;
        g_tx_buf[0] = 'A';

        // vTaskDelay(pdMS_TO_TICKS(500));

        err = RM_COMMS_USB_PCDC_Write(&g_comms_usb_pcdc0_ctrl, g_tx_buf, TX_BUF_LEN);
        if (FSP_SUCCESS != err)
        {
            /* Handle any errors. */
            R_GPT_PeriodSet(&g_timer5_ctrl, per / 4);
            R_GPT_DutyCycleSet(&g_timer5_ctrl, per / 8, GPT_IO_PIN_GTIOCA);
        }
        while ((0 == g_tx_flag) && (0 == g_err_flag))
        {
            /* Wait callback */
            R_GPT_PeriodSet(&g_timer5_ctrl, per / 8);
            R_GPT_DutyCycleSet(&g_timer5_ctrl, per / 16, GPT_IO_PIN_GTIOCA);
        }

        /* Receive data. */
        g_err_flag = 0;
        g_rx_flag = 0;
        err = RM_COMMS_USB_PCDC_Read(&g_comms_usb_pcdc0_ctrl, g_rx_buf, RX_BUF_LEN);
        if (FSP_SUCCESS != err)
        {
            /* Handle any errors.*/
        }
        while ((0 == g_rx_flag) && (0 == g_err_flag))
        {
            /* Wait callback */
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