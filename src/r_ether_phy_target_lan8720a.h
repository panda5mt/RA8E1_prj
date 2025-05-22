#include <hal_data.h>

void ether_phy_target_lan8720a_initialize(ether_phy_instance_ctrl_t *p_instance_ctrl);
bool ether_phy_target_lan8720_is_support_link_partner_ability(ether_phy_instance_ctrl_t *p_instance_ctrl,
                                                              uint32_t line_speed_duplex);