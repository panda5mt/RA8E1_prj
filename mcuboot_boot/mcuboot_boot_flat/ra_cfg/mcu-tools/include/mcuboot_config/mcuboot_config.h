/* generated configuration header file - do not edit */
#ifndef MCUBOOT_CONFIG_H_
#define MCUBOOT_CONFIG_H_


#ifdef __cplusplus
extern "C" {
#endif


#ifdef MCUBOOT_SIGN_RSA_LEN
#define MCUBOOT_SIGN_RSA
#endif


#ifdef MCUBOOT_OVERWRITE_ONLY_FAST
#define MCUBOOT_OVERWRITE_ONLY
#endif
#ifdef MCUBOOT_DIRECT_XIP_REVERT
#define MCUBOOT_DIRECT_XIP
#endif
#ifdef MCUBOOT_DIRECT_XIP
#if (1 == 0)
/* MMF support is enabled */
#define MCUBOOT_DIRECT_XIP_MMF

/* Start address of MMF region */
#define MCUBOOT_MMF_START_ADDR (0)
#endif
#endif

#if  ((1 == 1) || (1 == RA_NOT_DEFINED))
#define MCUBOOT_USE_TINYCRYPT
#if  (1 == RA_NOT_DEFINED)
#define RM_MCUBOOT_PORT_USE_TINYCRYPT_ACCELERATION
#endif
#elif (1 == RA_NOT_DEFINED)
#define MCUBOOT_USE_MBED_TLS
#elif (1 == RA_NOT_DEFINED)
#define MCUBOOT_USE_OCRYPTO
#if ((1 == RA_NOT_DEFINED) || (1 == RA_NOT_DEFINED))
#define RM_MCUBOOT_PORT_USE_OCRYPTO_PORT
#endif
#else
#define MCUBOOT_USE_USER_DEFINED_CRYPTO_STACK
#endif

#ifdef MCUBOOT_USE_USER_DEFINED_CRYPTO_STACK
#include "sce9_ecdsa_p256.h"
#endif

#include "bootutil/crypto/common.h"

#if (RA_NOT_DEFINED > 0)
#define RM_MCUBOOT_PORT_CFG_SECONDARY_USE_QSPI
#endif

#if (RA_NOT_DEFINED > 0)
#define RM_MCUBOOT_PORT_CFG_SECONDARY_USE_OSPI_B
#endif

#if ((RA_NOT_DEFINED > 0) || (RA_NOT_DEFINED > 0))
#define RM_MCUBOOT_PORT_CFG_SECONDARY_USE_XSPI
#endif

#define MCUBOOT_VALIDATE_PRIMARY_SLOT







#define MCUBOOT_DUALBANK_FLASH_LP (0)

#define MAX_BOOT_RECORD_SZ 0x64

#define MCUBOOT_SHARED_DATA_BASE 0x20000000

#define MCUBOOT_SHARED_DATA_SIZE 0x380

#define MCUBOOT_USE_FLASH_AREA_GET_SECTORS

#if (1 == (1))
#define MCUBOOT_HAVE_LOGGING    1
#endif


#if defined (MCUBOOT_ENCRYPT_EC256) || defined (MCUBOOT_ENCRYPT_RSA)
#ifndef _RA_BOOT_IMAGE
#define MCUBOOT_ENC_IMAGES
#endif
#endif

/*
 * Assertions
 */

/* Uncomment if your platform has its own mcuboot_config/mcuboot_assert.h.
 * If so, it must provide an ASSERT macro for use by bootutil. Otherwise,
 * "assert" is used. */
// #define MCUBOOT_HAVE_ASSERT_H

/*
 * Watchdog feeding
 */

#ifndef MCUBOOT_WATCHDOG_FEED
#define MCUBOOT_WATCHDOG_FEED()  
#endif

// do { /* Do nothing. */ } while (0)

#include "bsp_api.h"

#if BSP_FEATURE_MRAM_IS_AVAILABLE
 #define RM_MCUBOOT_MRAM_BLOCK_SIZE                (0x8000) /* 32 KB */
 #define RM_MCUBOOT_MRAM_PROGRAMMING_SIZE_BYTES    (BSP_FEATURE_MRAM_PROGRAMMING_SIZE_BYTES)
#endif

#if BSP_FEATURE_MRAM_IS_AVAILABLE
 #define FLASH_AREA_IMAGE_SECTOR_SIZE    (RM_MCUBOOT_MRAM_BLOCK_SIZE)
 #define MCUBOOT_BOOT_MAX_ALIGN          (RM_MCUBOOT_MRAM_PROGRAMMING_SIZE_BYTES)
#elif BSP_FEATURE_FLASH_HP_VERSION > 0
 #define FLASH_AREA_IMAGE_SECTOR_SIZE    (BSP_FEATURE_FLASH_HP_CF_REGION1_BLOCK_SIZE) /* 32 KB */
 #define MCUBOOT_BOOT_MAX_ALIGN          (BSP_FEATURE_FLASH_HP_CF_WRITE_SIZE)
#else
 #define FLASH_AREA_IMAGE_SECTOR_SIZE    (BSP_FEATURE_FLASH_LP_CF_BLOCK_SIZE)         /* 2 KB */
#endif

/* Maximum number of image sectors supported by the bootloader. */
#define MCUBOOT_MAX_IMG_SECTORS    ((RM_MCUBOOT_LARGEST_SECTOR) / \
                                       FLASH_AREA_IMAGE_SECTOR_SIZE)

#ifdef __cplusplus
}
#endif
#endif /* MCUBOOT_CONFIG_H_ */
