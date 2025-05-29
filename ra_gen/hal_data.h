/* generated HAL header file - do not edit */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "common_data.h"
#include "r_gpt.h"
#include "r_timer_api.h"
#include "r_ospi_b.h"
            #include "r_spi_flash_api.h"
#include "r_iic_master.h"
#include "r_i2c_master_api.h"
#include "r_capture_api.h"
            #include "r_ceu.h"
#include "r_sci_b_uart.h"
            #include "r_uart_api.h"
FSP_HEADER
/** Timer on GPT Instance. */
extern const timer_instance_t g_timer3;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_timer3_ctrl;
extern const timer_cfg_t g_timer3_cfg;

#ifndef NULL
void NULL(timer_callback_args_t * p_args);
#endif
/** Timer on GPT Instance. */
extern const timer_instance_t g_timer5;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_timer5_ctrl;
extern const timer_cfg_t g_timer5_cfg;

#ifndef NULL
void NULL(timer_callback_args_t * p_args);
#endif
#if OSPI_B_CFG_DMAC_SUPPORT_ENABLE
              #include "r_dmac.h"
            #endif
            #if OSPI_CFG_DOTF_SUPPORT_ENABLE
              #include "r_sce_if.h"
            #endif
            extern const spi_flash_instance_t g_ospi0;
            extern ospi_b_instance_ctrl_t g_ospi0_ctrl;
            extern const spi_flash_cfg_t g_ospi0_cfg;
/* I2C Master on IIC Instance. */
extern const i2c_master_instance_t g_i2c_master1;

/** Access the I2C Master instance using these structures when calling API functions directly (::p_api is not used). */
extern iic_master_instance_ctrl_t g_i2c_master1_ctrl;
extern const i2c_master_cfg_t g_i2c_master1_cfg;

#ifndef g_i2c_callback
void g_i2c_callback(i2c_master_callback_args_t * p_args);
#endif
/* CEU on CAPTURE instance */
            extern const capture_instance_t g_ceu0;
            /* Access the CEU instance using these structures when calling API functions directly (::p_api is not used). */
            extern ceu_instance_ctrl_t g_ceu0_ctrl;
            extern const capture_cfg_t g_ceu0_cfg;
            #ifndef g_ceu0_user_callback
            void g_ceu0_user_callback(capture_callback_args_t * p_args);
            #endif
/** UART on SCI Instance. */
            extern const uart_instance_t      g_uart9;

            /** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
            extern sci_b_uart_instance_ctrl_t     g_uart9_ctrl;
            extern const uart_cfg_t g_uart9_cfg;
            extern const sci_b_uart_extended_cfg_t g_uart9_cfg_extend;

            #ifndef NULL
            void NULL(uart_callback_args_t * p_args);
            #endif
void hal_entry(void);
void g_hal_init(void);
FSP_FOOTER
#endif /* HAL_DATA_H_ */
