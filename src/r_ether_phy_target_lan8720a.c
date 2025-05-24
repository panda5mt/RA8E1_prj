/*
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/

/* Access to peripherals and board defines. */
#include "bsp_api.h"
#include "r_ether_phy.h"
#include "r_ether_phy_target_lan8720a.h"

// #if (ETHER_PHY_CFG_TARGET_LAN8720A_ENABLE)

/***********************************************************************************************************************
 * Macro definitions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 * Typedef definitions
 ***********************************************************************************************************************/

#define ETHER_PHY_REG_BASIC_CONTROL (0x00)
#define ETHER_PHY_REG_BASIC_STATUS (0x01)
#define ETHER_PHY_REG_PHY_ID_1 (0x02)
#define ETHER_PHY_REG_PHY_ID_2 (0x03)

#define ETHER_PHY_REG_INTERRUPT_FLAG (29)
#define ETHER_PHY_REG_INTERRUPT_MASK (30)
#define ETHER_PHY_REG_PHY_CONTROL_1 (0x1F)

#define ETHER_PHY_LED_MODE_LED0_LINK_LED1_ACTIVITY (0x4000U)
#define ETHER_PHY_LED_MODE_MASK (0xC000U)

/***********************************************************************************************************************
 * Exported global variables (to be accessed by other files)
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 * Exported global function
 ***********************************************************************************************************************/
void ether_phy_target_lan8720a_initialize(ether_phy_instance_ctrl_t *p_instance_ctrl);
bool ether_phy_target_lan8720_is_support_link_partner_ability(ether_phy_instance_ctrl_t *p_instance_ctrl,
                                                              uint32_t line_speed_duplex);

/***********************************************************************************************************************
 * Private global variables and functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Function Name: ether_phy_targets_initialize
 * Description  : PHY-LSI specific initialization processing
 * Arguments    : p_api_ctrl -
 *                    Ethernet channel number
 * Return Value : none
 ***********************************************************************************************************************/
void ether_phy_target_lan8720a_initialize(ether_phy_instance_ctrl_t *p_instance_ctrl)
{
    uint32_t reg = 0x00;
    R_ETHER_PHY_Read(p_instance_ctrl, ETHER_PHY_REG_BASIC_CONTROL, &reg);  // read basic control reg
    R_ETHER_PHY_Read(p_instance_ctrl, ETHER_PHY_REG_BASIC_STATUS, &reg);   // read basic statusp reg
    R_ETHER_PHY_Read(p_instance_ctrl, ETHER_PHY_REG_INTERRUPT_FLAG, &reg); // clear interrupt

    do
    {
        reg = 0;
        R_ETHER_PHY_Read(&g_ether_phy0_ctrl, 0x02, &reg);
    } while (reg != 7); // PHY_ID=7 : Microchip

} /* End of function ether_phy_targets_initialize() */

/***********************************************************************************************************************
 * Function Name: ether_phy_targets_is_support_link_partner_ability
 * Description  : Check if the PHY-LSI connected Ethernet controller supports link ability
 * Arguments    : p_instance_ctrl -
 *                    Ethernet control block
 *                line_speed_duplex -
 *                    Line speed duplex of link partner PHY-LSI
 * Return Value : bool
 ***********************************************************************************************************************/
bool ether_phy_target_lan8720_is_support_link_partner_ability(ether_phy_instance_ctrl_t *p_instance_ctrl,
                                                              uint32_t line_speed_duplex)
{
    FSP_PARAMETER_NOT_USED(p_instance_ctrl);
    FSP_PARAMETER_NOT_USED(line_speed_duplex);

    /* This PHY-LSI supports half and full duplex mode. */
    return true;
} /* End of function ether_phy_targets_is_support_link_partner_ability() */

// #endif /* ETHER_PHY_CFG_TARGET_LAN8720A_ENABLE */
