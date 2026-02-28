/* generated configuration header file - do not edit */
#ifndef SYSFLASH_H_
#define SYSFLASH_H_
#ifndef __SYSFLASH_H__
#define __SYSFLASH_H__

#include "mcuboot_config/mcuboot_config.h"

#ifdef RM_MCUBOOT_PORT_CFG_SECONDARY_USE_OSPI_B
#include "mcuboot_config/mcuboot_ospi_b_config.h"
#endif

#include "bsp_api.h"
#include "bsp_linker_info.h"

/* Common macro for FSP header files. There is also a corresponding FSP_FOOTER macro at the end of this file. */
FSP_HEADER

#define FLASH_DEVICE_INTERNAL_FLASH    (0x7F)
#define FLASH_DEVICE_EXTERNAL_FLASH    (0x80)

#define FLASH_AREA_MCUBOOT_OFFSET     BSP_FEATURE_FLASH_CODE_FLASH_START

/* Define an offset if placing image at an address other than the start of the XSPI region. */
#ifndef XSPI_AREA_MCUBOOT_OFFSET
 #define XSPI_AREA_MCUBOOT_OFFSET      (0x0)
#endif

#if BSP_FEATURE_FLASH_HP_SUPPORTS_DUAL_BANK
 #define RM_MCUBOOT_DUAL_BANK_ENABLED    (!(0x7U & (BSP_CFG_OPTION_SETTING_DUALSEL)))
#elif BSP_FEATURE_FLASH_LP_SUPPORTS_DUAL_BANK
 #define RM_MCUBOOT_DUAL_BANK_ENABLED    MCUBOOT_DUALBANK_FLASH_LP
#endif


/* Common macro for FSP header files. There is also a corresponding FSP_HEADER macro at the top of this file. */
FSP_FOOTER

#endif /* __SYSFLASH_H__ */
#endif /* SYSFLASH_H_ */
