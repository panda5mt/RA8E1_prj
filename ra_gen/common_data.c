/* generated common source file - do not edit */
#include "common_data.h"

#if LWIP_PROVIDE_ERRNO
        int errno;
#endif
const ether_phy_lsi_cfg_t g_ether_phy_lsi0 =
{
    .address           = 0,
    .type              = ETHER_PHY_LSI_TYPE_CUSTOM,
};
ether_phy_instance_ctrl_t g_ether_phy0_ctrl;
#define RA_NOT_DEFINED (1)
const ether_phy_extended_cfg_t g_ether_phy0_extended_cfg =
{
    .p_target_init                     = ether_phy_target_lan8720a_initialize,
    .p_target_link_partner_ability_get = ether_phy_target_lan8720_is_support_link_partner_ability,
    .p_phy_lsi_cfg_list = {
#if (RA_NOT_DEFINED != g_ether_phy_lsi0)
    	&g_ether_phy_lsi0,
#else
    	NULL,
#endif
    },
};
#undef RA_NOT_DEFINED
const ether_phy_cfg_t g_ether_phy0_cfg =
{

    .channel                   = 0,
    .phy_lsi_address           = 0,
    .phy_reset_wait_time       = 0x00020000,
    .mii_bit_access_wait_time  = 8,
    .phy_lsi_type              = ETHER_PHY_LSI_TYPE_CUSTOM,
    .flow_control              = ETHER_PHY_FLOW_CONTROL_DISABLE,
    .mii_type                  = ETHER_PHY_MII_TYPE_RMII,
    .p_context                 = NULL,
    .p_extend                  = &g_ether_phy0_extended_cfg,

};
/* Instance structure to use this module. */
const ether_phy_instance_t g_ether_phy0 =
{
    .p_ctrl        = &g_ether_phy0_ctrl,
    .p_cfg         = &g_ether_phy0_cfg,
    .p_api         = &g_ether_phy_on_ether_phy
};
ether_instance_ctrl_t g_ether0_ctrl;

            uint8_t g_ether0_mac_address[6] = { 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF };

            __attribute__((__aligned__(16))) ether_instance_descriptor_t g_ether0_tx_descriptors[4] ETHER_BUFFER_PLACE_IN_SECTION;
            __attribute__((__aligned__(16))) ether_instance_descriptor_t g_ether0_rx_descriptors[4] ETHER_BUFFER_PLACE_IN_SECTION;

            

            

            const ether_extended_cfg_t g_ether0_extended_cfg_t =
            {
                .p_rx_descriptors   = g_ether0_rx_descriptors,
                .p_tx_descriptors   = g_ether0_tx_descriptors,
                .eesr_event_filter     = (ETHER_EESR_EVENT_MASK_RFOF | ETHER_EESR_EVENT_MASK_RDE | ETHER_EESR_EVENT_MASK_FR | ETHER_EESR_EVENT_MASK_TC | ETHER_EESR_EVENT_MASK_TABT | ETHER_EESR_EVENT_MASK_TWB |  0U),
                .ecsr_event_filter     = ( 0U),
            };

            const ether_cfg_t g_ether0_cfg =
            {
                .channel            = 0,
                .zerocopy           = ETHER_ZEROCOPY_ENABLE,
                .multicast          = ETHER_MULTICAST_ENABLE,
                .promiscuous        = ETHER_PROMISCUOUS_DISABLE,
                .flow_control       = ETHER_FLOW_CONTROL_ENABLE,
                .padding            = ETHER_PADDING_DISABLE,
                .padding_offset     = 0,
                .broadcast_filter   = 0,
                .p_mac_address      = g_ether0_mac_address,

                .num_tx_descriptors = 4,
                .num_rx_descriptors = 4,

                .pp_ether_buffers   = NULL,

                .ether_buffer_size  = 1536,

#if defined(VECTOR_NUMBER_EDMAC0_EINT)
                .irq                = VECTOR_NUMBER_EDMAC0_EINT,
#else
                .irq                = FSP_INVALID_VECTOR,
#endif

                .interrupt_priority = (10),

                .p_callback         = rm_lwip_ether_callback,
                .p_ether_phy_instance = &g_ether_phy0,
                .p_context          = &g_lwip_ether0_instance,
                .p_extend           = &g_ether0_extended_cfg_t,
            };

/* Instance structure to use this module. */
const ether_instance_t g_ether0 =
{
    .p_ctrl        = &g_ether0_ctrl,
    .p_cfg         = &g_ether0_cfg,
    .p_api         = &g_ether_on_ether
};
/* RX buffers. */
        LWIP_MEMPOOL_DECLARE(g_lwip_ether0_pbuf,  6, sizeof(rm_lwip_rx_pbuf_t), "RX PBUF pool");
        LWIP_MEMPOOL_DECLARE(g_lwip_ether0_rx_buffers,  10, 1514, "RX buffers pool");
        rm_lwip_ether_ctrl_t g_lwip_ether0_ctrl;

        rm_lwip_ether_cfg_t g_lwip_ether0_cfg =
        {
            .p_ether_instance       = (ether_instance_t *)(&g_ether0),
            .flags                  = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |  0U,
            .mtu                    = 1500,
            .link_check_interval    = 100,
            .p_rx_buffers_mempool   = &memp_g_lwip_ether0_rx_buffers,
            .p_rx_pbuf_mempool      = &memp_g_lwip_ether0_pbuf,
            .input_thread_stacksize = 1024,
            .input_thread_priority  = 4,
        };

        rm_lwip_ether_instance_t g_lwip_ether0_instance =
        {
            .p_ctrl = &g_lwip_ether0_ctrl,
            .p_cfg = &g_lwip_ether0_cfg,
        };
ioport_instance_ctrl_t g_ioport_ctrl;
const ioport_instance_t g_ioport =
        {
            .p_api = &g_ioport_on_ioport,
            .p_ctrl = &g_ioport_ctrl,
            .p_cfg = &g_bsp_pin_cfg,
        };
SemaphoreHandle_t g_usb_write_complete_binary_semaphore;
                #if 1
                StaticSemaphore_t g_usb_write_complete_binary_semaphore_memory;
                #endif
                void rtos_startup_err_callback(void * p_instance, void * p_data);
QueueHandle_t g_usb_read_queue;
                #if 1
                StaticQueue_t g_usb_read_queue_memory;
                uint8_t g_usb_read_queue_queue_memory[4 * 20];
                #endif
                void rtos_startup_err_callback(void * p_instance, void * p_data);
QueueHandle_t xQueueMes;
                #if 1
                StaticQueue_t xQueueMes_memory;
                uint8_t xQueueMes_queue_memory[64 * 3];
                #endif
                void rtos_startup_err_callback(void * p_instance, void * p_data);
void g_common_init(void) {
g_usb_write_complete_binary_semaphore =
                #if 1
                xSemaphoreCreateCountingStatic(
                #else
                xSemaphoreCreateCounting(
                #endif
                256,
                0
                #if 1
                , &g_usb_write_complete_binary_semaphore_memory
                #endif
                );
                if (NULL == g_usb_write_complete_binary_semaphore) {
                rtos_startup_err_callback(g_usb_write_complete_binary_semaphore, 0);
                }
g_usb_read_queue =
                #if 1
                xQueueCreateStatic(
                #else
                xQueueCreate(
                #endif
                20,
                4
                #if 1
                , &g_usb_read_queue_queue_memory[0],
                &g_usb_read_queue_memory
                #endif
                );
                if (NULL == g_usb_read_queue) {
                rtos_startup_err_callback(g_usb_read_queue, 0);
                }
xQueueMes =
                #if 1
                xQueueCreateStatic(
                #else
                xQueueCreate(
                #endif
                3,
                64
                #if 1
                , &xQueueMes_queue_memory[0],
                &xQueueMes_memory
                #endif
                );
                if (NULL == xQueueMes) {
                rtos_startup_err_callback(xQueueMes, 0);
                }
}
