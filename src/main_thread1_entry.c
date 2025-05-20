#include "main_thread1.h"
#include "hal_data.h"
#define ETHER_EXAMPLE_MAXIMUM_ETHERNET_FRAME_SIZE (1514)
#define ETHER_EXAMPLE_TRANSMIT_ETHERNET_FRAME_SIZE (60)
#define ETHER_EXAMPLE_SOURCE_MAC_ADDRESS 0x00, 0x11, 0x22, 0x33, 0x44, 0x55
#define ETHER_EXAMPLE_DESTINATION_MAC_ADDRESS 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
#define ETHER_EXAMPLE_FRAME_TYPE 0x00, 0x2E
#define ETHER_EXAMPLE_PAYLOAD 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

#define ETHER_EXAMPLE_FLAG_ON (1U)
#define ETHER_EXAMPLE_FLAG_OFF (0U)
#define ETHER_EXAMPLE_ETHER_ISR_EE_FR_MASK (1UL << 18)
#define ETHER_EXAMPLE_ETHER_ISR_EE_TC_MASK (1UL << 21)
#define ETHER_EXAMPLE_ETHER_ISR_EC_MPD_MASK (1UL << 1)
#define ETHER_EXAMPLE_ALIGNMENT_32_BYTE (32)
static volatile uint32_t g_example_receive_complete = 0;
static volatile uint32_t g_example_transfer_complete = 0;
static volatile uint32_t g_example_magic_packet_done = 0;

static uint8_t gp_send_data_internal[ETHER_EXAMPLE_TRANSMIT_ETHERNET_FRAME_SIZE] =
    {
        ETHER_EXAMPLE_DESTINATION_MAC_ADDRESS, /* Destination MAC address */
        ETHER_EXAMPLE_SOURCE_MAC_ADDRESS,      /* Source MAC address */
        ETHER_EXAMPLE_FRAME_TYPE,              /* Type field */
        ETHER_EXAMPLE_PAYLOAD                  /* Payload value (46byte) */
};

void ether_example_callback(ether_callback_args_t *p_args)
{
    switch (p_args->event)
    {
    case ETHER_EVENT_INTERRUPT:
    {
        if (ETHER_EXAMPLE_ETHER_ISR_EC_MPD_MASK == (p_args->status_ecsr & ETHER_EXAMPLE_ETHER_ISR_EC_MPD_MASK))
        {
            g_example_magic_packet_done = ETHER_EXAMPLE_FLAG_ON;
        }
        if (ETHER_EXAMPLE_ETHER_ISR_EE_TC_MASK == (p_args->status_eesr & ETHER_EXAMPLE_ETHER_ISR_EE_TC_MASK))
        {
            g_example_transfer_complete = ETHER_EXAMPLE_FLAG_ON;
        }
        if (ETHER_EXAMPLE_ETHER_ISR_EE_FR_MASK == (p_args->status_eesr & ETHER_EXAMPLE_ETHER_ISR_EE_FR_MASK))
        {
            g_example_receive_complete = ETHER_EXAMPLE_FLAG_ON;
        }
        break;
    }
    default:
    {
    }
    }
}
/* Main Thread1 entry function */
/* pvParameters contains TaskHandle_t */
void main_thread1_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);
    R_IOPORT_PinWrite(&g_ioport_ctrl, LAN8720_nRST, BSP_IO_LEVEL_LOW); // Reset LAN8720
    vTaskDelay(pdMS_TO_TICKS(100));
    R_IOPORT_PinWrite(&g_ioport_ctrl, LAN8720_nRST, BSP_IO_LEVEL_HIGH); // Start LAN8720

    /* TODO: add your own code here */
    while (1)
    {
        fsp_err_t err = FSP_SUCCESS;
        /* Source MAC Address */
        // static uint8_t mac_address_source[6] = {ETHER_EXAMPLE_SOURCE_MAC_ADDRESS};
        static uint8_t *p_read_buffer_nocopy;
        uint32_t read_data_size = 0;
        // g_ether0_cfg.p_mac_address = mac_address_source;
        // g_ether0_cfg.zerocopy = ETHER_ZEROCOPY_ENABLE;
        // g_ether0_cfg.p_callback = (void (*)(ether_callback_args_t *))ether_example_callback;
        /* Open the ether instance with initial configuration. */

        err = R_ETHER_Open(&g_ether0_ctrl, &g_ether0_cfg);
        /* Handle any errors. This function should be defined by the user. */
        assert(FSP_SUCCESS == err);
        do
        {
            /* When the Ethernet link status read from the PHY-LSI Basic Status register is link-up,
             * Initializes the module and make auto negotiation. */
            err = R_ETHER_LinkProcess(&g_ether0_ctrl);
        } while (FSP_SUCCESS != err);
        /* Set user buffer to TX descriptor and enable transmission. */
        err = R_ETHER_Write(&g_ether0_ctrl, (void *)gp_send_data_internal, sizeof(gp_send_data_internal));
        if (FSP_SUCCESS == err)
        {
            /* Wait for the transmission to complete. */
            /* Data array should not change in zero copy mode until transfer complete. */
            while (ETHER_EXAMPLE_FLAG_ON != g_example_transfer_complete)
            {
                ;
            }
        }
        /* Get receive buffer from RX descriptor. */
        err = R_ETHER_Read(&g_ether0_ctrl, (void *)&p_read_buffer_nocopy, &read_data_size);
        assert(FSP_SUCCESS == err);
        /* Process received data here */
        /* Release receive buffer to RX descriptor. */
        err = R_ETHER_BufferRelease(&g_ether0_ctrl);
        assert(FSP_SUCCESS == err);
        /* Disable transmission and receive function and close the ether instance. */
        R_ETHER_Close(&g_ether0_ctrl);

        vTaskDelay(1);
    }
}
