#include "main_thread2.h"
#define USB_APL_YES (1U)
#define USB_APL_NO (0U)
#define APL_NUM_USB_EVENT (10U)
// #define APL_USE_BAREMETAL_CALLBACK    USB_APL_NO
#define APL_USE_BAREMETAL_CALLBACK USB_APL_YES
#define DATA_LEN (16)
#define LINE_CODING_LENGTH (16)
/******************************************************************************
 * Private global variables and functions
 ******************************************************************************/
extern const usb_cfg_t g_basic0_cfg;
static uint8_t g_buf[DATA_LEN];
static usb_pcdc_linecoding_t g_line_coding;
extern uint8_t g_apl_device[];
extern uint8_t g_apl_configuration[];
extern uint8_t g_apl_hs_configuration[];
extern uint8_t g_apl_qualifier_descriptor[];
extern uint8_t *g_apl_string_table[];
// usb_instance_ctrl_t g_basic0_ctrl;
#if (BSP_CFG_RTOS == 2)
QueueHandle_t g_apl_mbx_hdl;
#endif /* (BSP_CFG_RTOS == 2) */
#if (BSP_CFG_RTOS == 0) && (APL_USE_BAREMETAL_CALLBACK == USB_YES)
usb_callback_args_t g_apl_usb_event;
usb_callback_args_t g_apl_usb_event_buf[APL_NUM_USB_EVENT];
uint8_t g_apl_usb_event_wp = 0;
uint8_t g_apl_usb_event_rp = 0;
#endif /* (BSP_CFG_RTOS == 0) && (APL_USE_BAREMETAL_CALLBACK == USB_YES) */

/* Main Thread2 entry function */
uint8_t ucHeap[configTOTAL_HEAP_SIZE];
/* pvParameters contains TaskHandle_t */
int g_err_flag, g_tx_flag, g_rx_flag;
usb_descriptor_t g_usb_descriptor;

/******************************************************************************
 * Exported global functions (to be accessed by other files)
 ******************************************************************************/
void usb_pcdc_example(void);
#if (BSP_CFG_RTOS == 0) && (APL_USE_BAREMETAL_CALLBACK == USB_YES)
void usb_apl_callback(usb_callback_args_t *p_event);
#endif
#if (BSP_CFG_RTOS == 2)
void usb_apl_callback(usb_event_info_t *p_api_event, usb_hdl_t cur_task, usb_onoff_t usb_state);
#endif

void main_thread2_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);
    usb_pcdc_example();
}
/******************************************************************************
 * Renesas Peripheral Communications Devices Class Sample Code functions
 ******************************************************************************/
#if (BSP_CFG_RTOS == 2)
/******************************************************************************
 * Function Name   : usb_apl_callback
 * Description     : Callback function for Application program
 * Arguments       : usb_event_info_t *p_api_event    : Control structure for USB API.
 *               : usb_hdl_t        cur_task        : Task Handle
 *               : uint8_t          usb_state       : USB_ON(USB_STATUS_REQUEST) / USB_OFF
 * Return value    : none
 ******************************************************************************/
void usb_apl_callback(usb_event_info_t *p_api_event, usb_hdl_t cur_task, usb_onoff_t usb_state)
{
    (void)usb_state;
    (void)cur_task;
    xQueueSend(g_apl_mbx_hdl, (const void *)&p_api_event, (TickType_t)(0));
} /* End of function usb_apl_callback */
#endif /* (BSP_CFG_RTOS == 2) */
/******************************************************************************
 * Function Name   : usb_apl_callback
 * Description     : Callback function for Application program
 * Arguments       : usb_callback_args_t * p_event    : Pointer to usb_callback_args_t structure
 * Return value    : none
 ******************************************************************************/
#if (BSP_CFG_RTOS == 0) && (APL_USE_BAREMETAL_CALLBACK == USB_YES)
void usb_apl_callback(usb_callback_args_t *p_event)
{
    g_apl_usb_event_buf[g_apl_usb_event_wp] = *p_event;
    g_apl_usb_event_wp++;
    g_apl_usb_event_wp %= APL_NUM_USB_EVENT;
}
#endif /* (BSP_CFG_RTOS == 0) && (APL_USE_BAREMETAL_CALLBACK == USB_YES) */
/******************************************************************************
 * Function Name   : usb_pcdc_example
 * Description     : Peripheral CDC application main process
 * Arguments       : none
 * Return value    : none
 ******************************************************************************/
void usb_pcdc_example(void)
{
    usb_event_info_t event_info;
    usb_status_t event = USB_STATUS_POWERED;
#if (BSP_CFG_RTOS == 2)
    usb_event_info_t *p_mess;
#endif
    g_usb_on_usb.open(&g_basic0_ctrl, &g_basic0_cfg);
#if (BSP_CFG_RTOS == 0) && (APL_USE_BAREMETAL_CALLBACK == USB_YES)
    g_usb_on_usb.callbackMemorySet(&g_basic0_ctrl, &g_apl_usb_event);
#endif /* (APL_USE_BAREMETAL_CALLBACK == USB_YES) */
    memset(&event_info, 0, sizeof(usb_event_info_t));
    while (1)
    {
#if (BSP_CFG_RTOS == 2) /* FreeRTOS */
        xQueueReceive(g_apl_mbx_hdl, (void *)&p_mess, portMAX_DELAY);
        event_info = *p_mess;
        event = event_info.event;
#else /* (BSP_CFG_RTOS == 2) */
#if (APL_USE_BAREMETAL_CALLBACK == USB_YES)
        g_usb_on_usb.driverActivate(&g_basic0_ctrl);
        if (g_apl_usb_event_wp != g_apl_usb_event_rp)
        {
            event_info = g_apl_usb_event_buf[g_apl_usb_event_rp];
            g_apl_usb_event_rp++;
            g_apl_usb_event_rp %= APL_NUM_USB_EVENT;
            event = event_info.event;
        }
#else /* (APL_USE_BAREMETAL_CALLBACK == USB_YES) */
        /* Get USB event data */
        g_usb_on_usb.eventGet(&event_info, &event);
#endif
#endif /* (BSP_CFG_RTOS == 2) */
        /* Handle the received event (if any) */
        switch (event)
        {
        case USB_STATUS_CONFIGURED:
        case USB_STATUS_WRITE_COMPLETE:
            /* Initialization complete; get data from host */
            g_usb_on_usb.read(&g_basic0_ctrl, g_buf, DATA_LEN, USB_CLASS_PCDC);
            break;
        case USB_STATUS_READ_COMPLETE:
            /* Loop back received data to host */
            g_usb_on_usb.write(&g_basic0_ctrl, g_buf, event_info.data_size, USB_CLASS_PCDC);
            break;
        case USB_STATUS_REQUEST: /* Receive Class Request */
            if (USB_PCDC_SET_LINE_CODING == (event_info.setup.request_type & USB_BREQUEST))
            {
                /* Configure virtual UART settings */
                g_usb_on_usb.periControlDataGet(&g_basic0_ctrl, (uint8_t *)&g_line_coding, LINE_CODING_LENGTH);
            }
            else if (USB_PCDC_GET_LINE_CODING == (event_info.setup.request_type & USB_BREQUEST))
            {
                /* Send virtual UART settings back to host */
                g_usb_on_usb.periControlDataSet(&g_basic0_ctrl, (uint8_t *)&g_line_coding, LINE_CODING_LENGTH);
            }
            else
            {
                /* ACK all other status requests */
                g_usb_on_usb.periControlStatusSet(&g_basic0_ctrl, USB_SETUP_STATUS_ACK);
            }
            break;
        case USB_STATUS_SUSPEND:
        case USB_STATUS_DETACH:
            break;
        default:
            break;
        }
    }
} /* End of function usb_pcdc_example() */