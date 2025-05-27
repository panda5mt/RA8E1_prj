#include "main_thread1.h"
#include "hal_data.h"

#include "putchar_ra8usb.h"
#include "r_ether_api.h"
#include "r_ether_phy_target_lan8720a.h"

#define ETHER_EXAMPLE_MAXIMUM_ETHERNET_FRAME_SIZE (1514)
#define ETHER_EXAMPLE_TRANSMIT_ETHERNET_FRAME_SIZE (1514)
#define ETHER_EXAMPLE_SOURCE_MAC_ADDRESS 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
#define ETHER_EXAMPLE_DESTINATION_MAC_ADDRESS 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
#define ETHER_EXAMPLE_FRAME_TYPE 0x00, 0x2E
#define ETHER_EXAMPLE_PAYLOAD 'I', '\'', 'm', ' ', 'R', 'A', '8', 'E', '1', '.', \
                              ' ', 'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r',  \
                              'l', 'd', '!', '!', ' ', 'H', 'E', 'L', 'L', 'O',  \
                              ' ', 'W', 'O', 'R', 'L', 'D', '!', ' ', 'R', 'e',  \
                              'n', 'e', 's', 'a', 's'

#define ETHER_EXAMPLE_FLAG_ON (1U)
#define ETHER_EXAMPLE_FLAG_OFF (0U)

#define ETHER_EXAMPLE_ALIGNMENT_32_BYTE (32)
static volatile uint32_t g_example_receive_complete = 0;
static volatile uint32_t g_example_transfer_complete = 0;
static volatile uint32_t g_example_link_on = 0;

#define PHY_BCR_RESET (1 << 15)
#define PHY_BCR_AUTONEGO_EN (1 << 12)
#define PHY_BCR_RESTART_AUTONEGO (1 << 9)
__attribute__((aligned(32))) uint8_t gp_send_data_internal[ETHER_EXAMPLE_TRANSMIT_ETHERNET_FRAME_SIZE] = {
    ETHER_EXAMPLE_DESTINATION_MAC_ADDRESS, /* Destination MAC address */
    ETHER_EXAMPLE_SOURCE_MAC_ADDRESS,      /* Source MAC address */
    ETHER_EXAMPLE_FRAME_TYPE,              /* Type field */
    ETHER_EXAMPLE_PAYLOAD                  /* Payload value (46byte) */
    // after bytes, filled with zeros
};

void ether_example_callback(ether_callback_args_t *p_args)
{
    switch (p_args->event)
    {
    case ETHER_EVENT_TX_COMPLETE:
        // xprintf("[ISR] TX COMPLETE.\n");
        g_example_transfer_complete = 1;
        break;

    case ETHER_EVENT_RX_COMPLETE:
        // xprintf("[ISR] RX COMPLETE.\n");
        g_example_receive_complete = 1;
        break;

    case ETHER_EVENT_LINK_ON:
        // xprintf("[ISR] LINK ON.\n");
        g_example_link_on = 1;
        break;
    case ETHER_EVENT_LINK_OFF:
        // xprintf("[ISR] LINK OFF.\n");
        g_example_link_on = 0;
        break;

    default:
        xprintf("[ISR] Event: %d\n", p_args->event);
        break;
    }
}

/* Main Thread1 entry function */
/* pvParameters contains TaskHandle_t */
void main_thread1_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    // START:LAN8720A Reset
    R_BSP_PinAccessEnable();
    R_BSP_PinWrite(LAN8720_nRST, BSP_IO_LEVEL_LOW); // Reset LAN8720
    vTaskDelay(pdMS_TO_TICKS(300));
    R_BSP_PinWrite(LAN8720_nRST, BSP_IO_LEVEL_HIGH); // Start LAN8720
    vTaskDelay(pdMS_TO_TICKS(300));
    // END:LAN8720A Reset

    fsp_err_t err = FSP_SUCCESS;

    err = R_ETHER_Open(&g_ether0_ctrl, &g_ether0_cfg);
    assert(FSP_SUCCESS == err);
    xprintf("[ETH] OPEN.\n");

    // Check Link ON
    g_example_link_on = 0;

    do
    {
        err = R_ETHER_LinkProcess(&g_ether0_ctrl);
        if (err == FSP_SUCCESS || g_example_link_on == 1)
        {
            g_example_link_on = 1;
            break;
        }
    } while (g_example_link_on != 1);

    xprintf("LINK ON\n");

    for (int i = 0; i < 100; i++)
    {
        g_example_transfer_complete = 0;
        /* Set user buffer to TX descriptor and enable transmission. */
        err = R_ETHER_Write(&g_ether0_ctrl, (void *)gp_send_data_internal, sizeof(gp_send_data_internal));
        assert(FSP_SUCCESS == err);
        if (FSP_SUCCESS == err)
        {
            /* Wait for the transmission to complete. */
            /* Data array should not change in zero copy mode until transfer complete. */
            while (ETHER_EXAMPLE_FLAG_ON != g_example_transfer_complete)
            {
                ;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        xprintf("[ETH]Write OK!\n");
    }
    /* Get receive buffer from RX descriptor. */
    static uint8_t *p_read_buffer_nocopy;
    uint32_t read_data_size = 0;
    g_example_receive_complete = 0;
    err = R_ETHER_Read(&g_ether0_ctrl, (void *)&p_read_buffer_nocopy, &read_data_size);
    xprintf("[ETH] RCV result:%d\n", err);
    assert(FSP_SUCCESS == err);

    /* Process received data here */
    if (FSP_SUCCESS == err)
    {
        /* Wait for the transmission to complete. */
        /* Data array should not change in zero copy mode until transfer complete. */
        while (ETHER_EXAMPLE_FLAG_ON != g_example_receive_complete)
        {
            ;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    xprintf("[ETH]RCV OK!\n");
    /* Release receive buffer to RX descriptor. */
    err = R_ETHER_BufferRelease(&g_ether0_ctrl);
    assert(FSP_SUCCESS == err);

    // /* Disable transmission and receive function and close the ether instance. */
    R_ETHER_Close(&g_ether0_ctrl);
    xprintf("[ETH]Close.\n");
    vTaskSuspend(NULL);
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
