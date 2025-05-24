#include "main_thread1.h"
#include "hal_data.h"

#include "putchar_ra8usb.h"
#include "r_ether_phy.h"
#include "r_ether_phy_target_lan8720a.h"
#include "ra/fsp/src/bsp/cmsis/Device/RENESAS/Include/R7FA8E1AF.h"

#define ETHER_EXAMPLE_MAXIMUM_ETHERNET_FRAME_SIZE (1514)
#define ETHER_EXAMPLE_TRANSMIT_ETHERNET_FRAME_SIZE (1514 * 2)
#define ETHER_EXAMPLE_SOURCE_MAC_ADDRESS 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
#define ETHER_EXAMPLE_DESTINATION_MAC_ADDRESS 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
#define ETHER_EXAMPLE_FRAME_TYPE 0x00, 0x2E
#define ETHER_EXAMPLE_PAYLOAD 'I', '\'', 'm', ' ', 'R', 'A', '8', 'E', '1', '.', \
                              'U', 'N', 'K', 'O', '!', 'U', 'N', 'K', 'O', '!',  \
                              'U', 'N', 'K', 'O', '!', 'U', 'N', 'K', 'O', '!',  \
                              'U', 'N', 'K', 'O', '!', 'U', 'N', 'K', 'O', '!',  \
                              'U', 'N', 'K', 'O', '!'

#define ETHER_EXAMPLE_FLAG_ON (1U)
#define ETHER_EXAMPLE_FLAG_OFF (0U)
#define ETHER_EXAMPLE_ETHER_ISR_EE_FR_MASK (1UL << 18)
#define ETHER_EXAMPLE_ETHER_ISR_EE_TC_MASK (1UL << 21)
#define ETHER_EXAMPLE_ETHER_ISR_EC_MPD_MASK (1UL << 1)
#define ETHER_EXAMPLE_ALIGNMENT_32_BYTE (32)
static volatile uint32_t g_example_receive_complete = 0;
static volatile uint32_t g_example_transfer_complete = 0;
static volatile uint32_t g_example_magic_packet_done = 0;
#define PHY_REG_BCR 0x00 // Basic Control Register
#define PHY_REG_BSR 0x01 // Basic Status Register
#define PHY_REG_PHYID1 0x02
#define PHY_REG_PHYID2 0x03

#define PHY_BCR_RESET (1 << 15)
#define PHY_BCR_AUTONEGO_EN (1 << 12)
#define PHY_BCR_RESTART_AUTONEGO (1 << 9)

__attribute__((aligned(32))) uint8_t gp_send_data_internal[ETHER_EXAMPLE_TRANSMIT_ETHERNET_FRAME_SIZE] = {
    ETHER_EXAMPLE_DESTINATION_MAC_ADDRESS, /* Destination MAC address */
    ETHER_EXAMPLE_SOURCE_MAC_ADDRESS,      /* Source MAC address */
    ETHER_EXAMPLE_FRAME_TYPE,              /* Type field */
    ETHER_EXAMPLE_PAYLOAD                  /* Payload value (46byte) */
};

void ether_example_callback(ether_callback_args_t *p_args)
{
    switch (p_args->event)
    {
    case ETHER_EVENT_TX_COMPLETE:
        xprintf("[ETH] TX COMPLETE.\n");
        g_example_transfer_complete = 1;
        break;

    case ETHER_EVENT_RX_COMPLETE:
        xprintf("[ETH] RX COMPLETE.\n");
        g_example_receive_complete = 1;
        break;

    case ETHER_EVENT_LINK_ON:
        xprintf("[ETH] LINK ON.\n");
        break;

    default:
        xprintf("[ETH] Event: %d\n", p_args->event);
        break;
    }
}

/* Main Thread1 entry function */
/* pvParameters contains TaskHandle_t */
void main_thread1_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);
    R_BSP_PinAccessEnable();
    R_BSP_PinWrite(LAN8720_nRST, BSP_IO_LEVEL_LOW); // Reset LAN8720
    xprintf("GPIO = L\n");
    vTaskDelay(pdMS_TO_TICKS(1000));

    R_BSP_PinWrite(LAN8720_nRST, BSP_IO_LEVEL_HIGH); // Start LAN8720
    xprintf("GPIO = H\n");
    vTaskDelay(pdMS_TO_TICKS(1000));

    fsp_err_t err = FSP_SUCCESS;
    static uint8_t *p_read_buffer_nocopy;
    uint32_t read_data_size = 0;

    // NVIC_EnableIRQ(EDMAC0_EINT_IRQn);
    err = R_ETHER_Open(&g_ether0_ctrl, &g_ether0_cfg);
    assert(FSP_SUCCESS == err);
    xprintf("[ETH] OPEN.\n");

    do
    {
        err = R_ETHER_LinkProcess(&g_ether0_ctrl);
        // xprintf("[ETH] LinkProcess result: %d\n", err); // ← エラーコード確認
    } while (FSP_SUCCESS != err);

    xprintf("[ETH]LINK OK.\n");

    uint32_t id1 = 0, id2 = 0;

    if (R_ETHER_PHY_Read(&g_ether_phy0_ctrl, 0x02, &id1) != FSP_SUCCESS ||
        R_ETHER_PHY_Read(&g_ether_phy0_ctrl, 0x03, &id2) != FSP_SUCCESS)
    {
        xprintf("PHY init fail"); // 読み出し失敗
    }
    xprintf("ID1=%X,ID2=%X\n", id1, id2);

    uint32_t bsr = 0;
    do
    {
        R_ETHER_PHY_Read(&g_ether_phy0_ctrl, 0x01, &bsr);
        xprintf("LINK STATUS:%X\n", bsr);
    } while (bsr != 0x782d);

    g_example_transfer_complete = 0;
    /* Set user buffer to TX descriptor and enable transmission. */
    err = R_ETHER_Write(&g_ether0_ctrl, (void *)gp_send_data_internal, sizeof(gp_send_data_internal));
    xprintf("[ETH]result:%d\n", err); //
    if (FSP_SUCCESS == err)
    {
        /* Wait for the transmission to complete. */
        /* Data array should not change in zero copy mode until transfer complete. */
        while (ETHER_EXAMPLE_FLAG_ON != g_example_transfer_complete)
        {
            ;
        }
    }
    xprintf("[ETH]OK!\n");
    /* Get receive buffer from RX descriptor. */
    err = R_ETHER_Read(&g_ether0_ctrl, (void *)&p_read_buffer_nocopy, &read_data_size);
    assert(FSP_SUCCESS == err);
    /* Process received data here */
    /* Release receive buffer to RX descriptor. */
    err = R_ETHER_BufferRelease(&g_ether0_ctrl);
    assert(FSP_SUCCESS == err);
    /* Disable transmission and receive function and close the ether instance. */
    R_ETHER_Close(&g_ether0_ctrl);
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
