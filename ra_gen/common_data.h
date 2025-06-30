/* generated common header file - do not edit */
#ifndef COMMON_DATA_H_
#define COMMON_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "FreeRTOS.h"
                #include "semphr.h"
#include "FreeRTOS.h"
                #include "queue.h"
#include "r_ether_phy.h"
#include "r_ether_phy_api.h"
#include "r_ether.h"
#include "r_ether_api.h"
#include "rm_lwip_ether.h"
#include "arm_math.h"
#include "r_ioport.h"
#include "bsp_pin_cfg.h"
FSP_HEADER
#ifndef ETHER_PHY_LSI_TYPE_KIT_COMPONENT
  #define ETHER_PHY_LSI_TYPE_KIT_COMPONENT ETHER_PHY_LSI_TYPE_DEFAULT
#endif

#ifndef ether_phy_target_lan8720a_initialize
void ether_phy_target_lan8720a_initialize(ether_phy_instance_ctrl_t * p_instance_ctrl);
#endif

#ifndef ether_phy_target_lan8720_is_support_link_partner_ability
bool ether_phy_target_lan8720_is_support_link_partner_ability(ether_phy_instance_ctrl_t * p_instance_ctrl, uint32_t line_speed_duplex);
#endif

/** ether_phy on ether_phy Instance. */
extern const ether_phy_instance_t g_ether_phy0;

/** Access the Ethernet PHY instance using these structures when calling API functions directly (::p_api is not used). */
extern ether_phy_instance_ctrl_t g_ether_phy0_ctrl;
extern const ether_phy_cfg_t g_ether_phy0_cfg;
extern const ether_phy_extended_cfg_t g_ether_phy0_extended_cfg;
#if (BSP_FEATURE_TZ_HAS_TRUSTZONE == 1)  && (BSP_TZ_NONSECURE_BUILD != 1) && (BSP_FEATURE_ETHER_SUPPORTS_TZ_SECURE == 0)
#define ETHER_BUFFER_PLACE_IN_SECTION BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".ns_buffer")
#else
#define ETHER_BUFFER_PLACE_IN_SECTION
#endif

/** ether on ether Instance. */
extern const ether_instance_t g_ether0;

/** Access the Ethernet instance using these structures when calling API functions directly (::p_api is not used). */
extern ether_instance_ctrl_t g_ether0_ctrl;
extern const ether_cfg_t g_ether0_cfg;

#ifndef rm_lwip_ether_callback
void rm_lwip_ether_callback(ether_callback_args_t * p_args);
#endif
/* Instance for lwIP Ethernet Driver */
extern rm_lwip_ether_instance_t g_lwip_ether0_instance;
extern rm_lwip_ether_ctrl_t g_lwip_ether0_ctrl;
extern rm_lwip_ether_cfg_t g_lwip_ether0_cfg;
#define IOPORT_CFG_NAME g_bsp_pin_cfg
#define IOPORT_CFG_OPEN R_IOPORT_Open
#define IOPORT_CFG_CTRL g_ioport_ctrl

/* IOPORT Instance */
extern const ioport_instance_t g_ioport;

/* IOPORT control structure. */
extern ioport_instance_ctrl_t g_ioport_ctrl;
extern SemaphoreHandle_t g_usb_write_complete_binary_semaphore;
extern QueueHandle_t g_usb_read_queue;
extern QueueHandle_t xQueueMes;
void g_common_init(void);
FSP_FOOTER
#endif /* COMMON_DATA_H_ */
