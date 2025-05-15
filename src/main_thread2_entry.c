#include "main_thread2.h"
/* Main Thread2 entry function */

#include "usb_cdc.h"
/* New Thread entry function */

uint32_t g_usb_read_data;

/* 115200 8n1 by default */
static usb_pcdc_linecoding_t g_line_coding = {
    .dw_dte_rate = 115200,
    .b_char_format = STOP_BITS_1,
    .b_parity_type = PARITY_NONE,
    .b_data_bits = DATA_BITS_8,
};

static volatile usb_pcdc_ctrllinestate_t g_control_line_state = {
    .bdtr = 0,
    .brts = 0,
};

void usb_cdc_rtos_callback(usb_event_info_t *event, usb_hdl_t handle, usb_onoff_t onoff)
{
    FSP_PARAMETER_NOT_USED(handle);
    FSP_PARAMETER_NOT_USED(onoff);

    usb_setup_t setup;

    switch (event->event)
    {
    case USB_STATUS_CONFIGURED:
        break;
    case USB_STATUS_WRITE_COMPLETE:
        if (pdTRUE == xSemaphoreGiveFromISR(g_usb_write_complete_binary_semaphore, NULL))
        {
            __NOP();
        }
        break;
    case USB_STATUS_READ_COMPLETE:
        if (pdTRUE == xQueueSendFromISR(g_usb_read_queue, &event->data_size, NULL))
        {
            __NOP();
        }

        break;
    case USB_STATUS_REQUEST: /* Receive Class Request */
        g_usb_on_usb.setupGet(event, &setup);

        if (USB_PCDC_SET_LINE_CODING == (setup.request_type & USB_BREQUEST))
        {
            g_usb_on_usb.periControlDataGet(event, (uint8_t *)&g_line_coding, LINE_CODING_LENGTH);
        }
        else if (USB_PCDC_GET_LINE_CODING == (setup.request_type & USB_BREQUEST))
        {
            g_usb_on_usb.periControlDataSet(event, (uint8_t *)&g_line_coding, LINE_CODING_LENGTH);
        }
        else if (USB_PCDC_SET_CONTROL_LINE_STATE == (event->setup.request_type & USB_BREQUEST))
        {
            fsp_err_t err = g_usb_on_usb.periControlDataGet(event, (uint8_t *)&g_control_line_state, sizeof(g_control_line_state));
            if (FSP_SUCCESS == err)
            {
                g_control_line_state.bdtr = (unsigned char)((event->setup.request_value >> 0) & 0x01);
                g_control_line_state.brts = (unsigned char)((event->setup.request_value >> 1) & 0x01);
            }
        }
        else
        {
            /* none */
        }
        break;
    case USB_STATUS_REQUEST_COMPLETE:
        __NOP();
        break;
    case USB_STATUS_SUSPEND:
    case USB_STATUS_DETACH:
    case USB_STATUS_DEFAULT:
        __NOP();
        break;
    default:
        __NOP();
        break;
    }
}

/* pvParameters contains TaskHandle_t */
void main_thread2_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);
    fsp_err_t err;

    err = g_usb_on_usb.open(&g_basic0_ctrl, &g_basic0_cfg);
    if (FSP_SUCCESS != err)
    {
        while (1)
            ;
    }

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

    /* Wait for the application to open the COM port */
    while (0 == g_control_line_state.bdtr)
    {
        vTaskDelay(1);
    }

    vTaskDelay(150 / portTICK_PERIOD_MS); // Delay before first write over USB CDC
    /* TODO: add your own code here */

    while (1)
    {

        err = g_usb_on_usb.write(&g_basic0_ctrl, (uint8_t *)"Test String\r\n", strlen("Test String\r\n"), USB_CLASS_PCDC);
        if (FSP_SUCCESS != err)
        {
            __BKPT(0);
        }

        /* Wait for the USB Write to complete */
        if (xSemaphoreTake(g_usb_write_complete_binary_semaphore, portMAX_DELAY) == pdTRUE)
        {
            __NOP(); // The write has completed
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay
    }
}
