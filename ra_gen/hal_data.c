/* generated HAL source file - do not edit */
#include "hal_data.h"


gpt_instance_ctrl_t g_timer3_ctrl;
#if 0
const gpt_extended_pwm_cfg_t g_timer3_pwm_extend =
{
    .trough_ipl          = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT3_COUNTER_UNDERFLOW)
    .trough_irq          = VECTOR_NUMBER_GPT3_COUNTER_UNDERFLOW,
#else
    .trough_irq          = FSP_INVALID_VECTOR,
#endif
    .poeg_link           = GPT_POEG_LINK_POEG0,
    .output_disable      = (gpt_output_disable_t) ( GPT_OUTPUT_DISABLE_NONE),
    .adc_trigger         = (gpt_adc_trigger_t) ( GPT_ADC_TRIGGER_NONE),
    .dead_time_count_up  = 0,
    .dead_time_count_down = 0,
    .adc_a_compare_match = 0,
    .adc_b_compare_match = 0,
    .interrupt_skip_source = GPT_INTERRUPT_SKIP_SOURCE_NONE,
    .interrupt_skip_count  = GPT_INTERRUPT_SKIP_COUNT_0,
    .interrupt_skip_adc    = GPT_INTERRUPT_SKIP_ADC_NONE,
    .gtioca_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
    .gtiocb_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
};
#endif
const gpt_extended_cfg_t g_timer3_extend =
{
    .gtioca = { .output_enabled = true,
                .stop_level     = GPT_PIN_LEVEL_LOW
              },
    .gtiocb = { .output_enabled = true,
                .stop_level     = GPT_PIN_LEVEL_LOW
              },
    .start_source        = (gpt_source_t) ( GPT_SOURCE_NONE),
    .stop_source         = (gpt_source_t) ( GPT_SOURCE_NONE),
    .clear_source        = (gpt_source_t) ( GPT_SOURCE_NONE),
    .count_up_source     = (gpt_source_t) ( GPT_SOURCE_NONE),
    .count_down_source   = (gpt_source_t) ( GPT_SOURCE_NONE),
    .capture_a_source    = (gpt_source_t) ( GPT_SOURCE_NONE),
    .capture_b_source    = (gpt_source_t) ( GPT_SOURCE_NONE),
    .capture_a_ipl       = (BSP_IRQ_DISABLED),
    .capture_b_ipl       = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT3_CAPTURE_COMPARE_A)
    .capture_a_irq       = VECTOR_NUMBER_GPT3_CAPTURE_COMPARE_A,
#else
    .capture_a_irq       = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT3_CAPTURE_COMPARE_B)
    .capture_b_irq       = VECTOR_NUMBER_GPT3_CAPTURE_COMPARE_B,
#else
    .capture_b_irq       = FSP_INVALID_VECTOR,
#endif
     .compare_match_value = { /* CMP_A */ (uint32_t)0x0, /* CMP_B */ (uint32_t)0x0}, .compare_match_status = (0U << 1U) | 0U,
    .capture_filter_gtioca       = GPT_CAPTURE_FILTER_NONE,
    .capture_filter_gtiocb       = GPT_CAPTURE_FILTER_NONE,
#if 0
    .p_pwm_cfg                   = &g_timer3_pwm_extend,
#else
    .p_pwm_cfg                   = NULL,
#endif
#if 0
    .gtior_setting.gtior_b.gtioa  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.oadflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.oahld  = 0U,
    .gtior_setting.gtior_b.oae    = (uint32_t) true,
    .gtior_setting.gtior_b.oadf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfaen  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsa  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
    .gtior_setting.gtior_b.gtiob  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.obdflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.obhld  = 0U,
    .gtior_setting.gtior_b.obe    = (uint32_t) true,
    .gtior_setting.gtior_b.obdf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfben  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsb  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
#else
    .gtior_setting.gtior = 0U,
#endif
};

const timer_cfg_t g_timer3_cfg =
{
    .mode                = TIMER_MODE_PERIODIC,
    /* Actual period: 4.166666666666667e-8 seconds. Actual duty: 40%. */ .period_counts = (uint32_t) 0x5, .duty_cycle_counts = 0x2, .source_div = (timer_source_div_t)0,
    .channel             = 3,
    .p_callback          = NULL,
    /** If NULL then do not add & */
#if defined(NULL)
    .p_context           = NULL,
#else
    .p_context           = &NULL,
#endif
    .p_extend            = &g_timer3_extend,
    .cycle_end_ipl       = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT3_COUNTER_OVERFLOW)
    .cycle_end_irq       = VECTOR_NUMBER_GPT3_COUNTER_OVERFLOW,
#else
    .cycle_end_irq       = FSP_INVALID_VECTOR,
#endif
};
/* Instance structure to use this module. */
const timer_instance_t g_timer3 =
{
    .p_ctrl        = &g_timer3_ctrl,
    .p_cfg         = &g_timer3_cfg,
    .p_api         = &g_timer_on_gpt
};
gpt_instance_ctrl_t g_timer5_ctrl;
#if 0
const gpt_extended_pwm_cfg_t g_timer5_pwm_extend =
{
    .trough_ipl          = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT5_COUNTER_UNDERFLOW)
    .trough_irq          = VECTOR_NUMBER_GPT5_COUNTER_UNDERFLOW,
#else
    .trough_irq          = FSP_INVALID_VECTOR,
#endif
    .poeg_link           = GPT_POEG_LINK_POEG0,
    .output_disable      = (gpt_output_disable_t) ( GPT_OUTPUT_DISABLE_NONE),
    .adc_trigger         = (gpt_adc_trigger_t) ( GPT_ADC_TRIGGER_NONE),
    .dead_time_count_up  = 0,
    .dead_time_count_down = 0,
    .adc_a_compare_match = 0,
    .adc_b_compare_match = 0,
    .interrupt_skip_source = GPT_INTERRUPT_SKIP_SOURCE_NONE,
    .interrupt_skip_count  = GPT_INTERRUPT_SKIP_COUNT_0,
    .interrupt_skip_adc    = GPT_INTERRUPT_SKIP_ADC_NONE,
    .gtioca_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
    .gtiocb_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
};
#endif
const gpt_extended_cfg_t g_timer5_extend =
{
    .gtioca = { .output_enabled = true,
                .stop_level     = GPT_PIN_LEVEL_LOW
              },
    .gtiocb = { .output_enabled = true,
                .stop_level     = GPT_PIN_LEVEL_LOW
              },
    .start_source        = (gpt_source_t) ( GPT_SOURCE_NONE),
    .stop_source         = (gpt_source_t) ( GPT_SOURCE_NONE),
    .clear_source        = (gpt_source_t) ( GPT_SOURCE_NONE),
    .count_up_source     = (gpt_source_t) ( GPT_SOURCE_NONE),
    .count_down_source   = (gpt_source_t) ( GPT_SOURCE_NONE),
    .capture_a_source    = (gpt_source_t) ( GPT_SOURCE_NONE),
    .capture_b_source    = (gpt_source_t) ( GPT_SOURCE_NONE),
    .capture_a_ipl       = (BSP_IRQ_DISABLED),
    .capture_b_ipl       = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT5_CAPTURE_COMPARE_A)
    .capture_a_irq       = VECTOR_NUMBER_GPT5_CAPTURE_COMPARE_A,
#else
    .capture_a_irq       = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT5_CAPTURE_COMPARE_B)
    .capture_b_irq       = VECTOR_NUMBER_GPT5_CAPTURE_COMPARE_B,
#else
    .capture_b_irq       = FSP_INVALID_VECTOR,
#endif
     .compare_match_value = { /* CMP_A */ (uint32_t)0x0, /* CMP_B */ (uint32_t)0x0}, .compare_match_status = (0U << 1U) | 0U,
    .capture_filter_gtioca       = GPT_CAPTURE_FILTER_NONE,
    .capture_filter_gtiocb       = GPT_CAPTURE_FILTER_NONE,
#if 0
    .p_pwm_cfg                   = &g_timer5_pwm_extend,
#else
    .p_pwm_cfg                   = NULL,
#endif
#if 0
    .gtior_setting.gtior_b.gtioa  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.oadflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.oahld  = 0U,
    .gtior_setting.gtior_b.oae    = (uint32_t) true,
    .gtior_setting.gtior_b.oadf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfaen  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsa  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
    .gtior_setting.gtior_b.gtiob  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.obdflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.obhld  = 0U,
    .gtior_setting.gtior_b.obe    = (uint32_t) true,
    .gtior_setting.gtior_b.obdf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfben  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsb  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
#else
    .gtior_setting.gtior = 0U,
#endif
};

const timer_cfg_t g_timer5_cfg =
{
    .mode                = TIMER_MODE_PERIODIC,
    /* Actual period: 0.5 seconds. Actual duty: 50%. */ .period_counts = (uint32_t) 0x3938700, .duty_cycle_counts = 0x1c9c380, .source_div = (timer_source_div_t)0,
    .channel             = 5,
    .p_callback          = NULL,
    /** If NULL then do not add & */
#if defined(NULL)
    .p_context           = NULL,
#else
    .p_context           = &NULL,
#endif
    .p_extend            = &g_timer5_extend,
    .cycle_end_ipl       = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT5_COUNTER_OVERFLOW)
    .cycle_end_irq       = VECTOR_NUMBER_GPT5_COUNTER_OVERFLOW,
#else
    .cycle_end_irq       = FSP_INVALID_VECTOR,
#endif
};
/* Instance structure to use this module. */
const timer_instance_t g_timer5 =
{
    .p_ctrl        = &g_timer5_ctrl,
    .p_cfg         = &g_timer5_cfg,
    .p_api         = &g_timer_on_gpt
};
ether_phy_instance_ctrl_t g_ether_phy0_ctrl;

const ether_phy_extended_cfg_t g_ether_phy0_extended_cfg =
{
    .p_target_init                     = NULL,
    .p_target_link_partner_ability_get = NULL

};

const ether_phy_cfg_t g_ether_phy0_cfg =
{

    .channel                   = 0,
    .phy_lsi_address           = 0x00,
    .phy_reset_wait_time       = 0x00020000,
    .mii_bit_access_wait_time  = 8,
    .phy_lsi_type              = ETHER_PHY_LSI_TYPE_KSZ8041,
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

            __attribute__((__aligned__(16))) ether_instance_descriptor_t g_ether0_tx_descriptors[1] ETHER_BUFFER_PLACE_IN_SECTION;
            __attribute__((__aligned__(16))) ether_instance_descriptor_t g_ether0_rx_descriptors[1] ETHER_BUFFER_PLACE_IN_SECTION;

            __attribute__((__aligned__(32)))uint8_t g_ether0_ether_buffer0[1536]ETHER_BUFFER_PLACE_IN_SECTION;


            uint8_t *pp_g_ether0_ether_buffers[1] = {
(uint8_t *) &g_ether0_ether_buffer0[0],
};

            const ether_extended_cfg_t g_ether0_extended_cfg_t =
            {
                .p_rx_descriptors   = g_ether0_rx_descriptors,
                .p_tx_descriptors   = g_ether0_tx_descriptors,
                .eesr_event_filter     = (ETHER_EESR_EVENT_MASK_RFOF | ETHER_EESR_EVENT_MASK_RDE | ETHER_EESR_EVENT_MASK_FR | ETHER_EESR_EVENT_MASK_TFUF | ETHER_EESR_EVENT_MASK_TDE | ETHER_EESR_EVENT_MASK_TC |  0U),
                .ecsr_event_filter     = ( 0U),
            };

            const ether_cfg_t g_ether0_cfg =
            {
                .channel            = 0,
                .zerocopy           = ETHER_ZEROCOPY_ENABLE,
                .multicast          = ETHER_MULTICAST_DISABLE,
                .promiscuous        = ETHER_PROMISCUOUS_DISABLE,
                .flow_control       = ETHER_FLOW_CONTROL_DISABLE,
                .padding            = ETHER_PADDING_DISABLE,
                .padding_offset     = 0,
                .broadcast_filter   = 0,
                .p_mac_address      = g_ether0_mac_address,

                .num_tx_descriptors = 1,
                .num_rx_descriptors = 1,

                .pp_ether_buffers   = pp_g_ether0_ether_buffers,

                .ether_buffer_size  = 1536,

#if defined(VECTOR_NUMBER_EDMAC0_EINT)
                .irq                = VECTOR_NUMBER_EDMAC0_EINT,
#else
                .irq                = FSP_INVALID_VECTOR,
#endif

                .interrupt_priority = (12),

                .p_callback         = ether_example_callback,
                .p_ether_phy_instance = &g_ether_phy0,
                .p_context          = NULL,
                .p_extend           = &g_ether0_extended_cfg_t,
            };

/* Instance structure to use this module. */
const ether_instance_t g_ether0 =
{
    .p_ctrl        = &g_ether0_ctrl,
    .p_cfg         = &g_ether0_cfg,
    .p_api         = &g_ether_on_ether
};
ospi_b_instance_ctrl_t g_ospi0_ctrl;

            static const spi_flash_erase_command_t g_ospi0_erase_command_list[] =
            {
            #if ((0x2121 > 0) && (4096 > 0))
                {.command = 0x2121,     .size = 4096 },
            #endif
            #if ((0xDCDC > 0) && (262144 > 0))
                {.command = 0xDCDC,      .size = 262144  },
            #endif
            #if (0x6060 > 0)
                {.command = 0x6060,       .size  = SPI_FLASH_ERASE_SIZE_CHIP_ERASE        },
            #endif
            };

            static ospi_b_timing_setting_t g_ospi0_timing_settings =
            {
                .command_to_command_interval = OSPI_B_COMMAND_INTERVAL_CLOCKS_2,
                .cs_pullup_lag               = OSPI_B_COMMAND_CS_PULLUP_CLOCKS_NO_EXTENSION,
                .cs_pulldown_lead            = OSPI_B_COMMAND_CS_PULLDOWN_CLOCKS_NO_EXTENSION
            };

            #if !(0)

             #if (0)
            static const spi_flash_erase_command_t g_ospi0_high_speed_erase_command_list[] =
            {
              #if ((0 > 0) && (4096 > 0))
                {.command = 0,     .size = 4096 },
              #endif
              #if ((0 > 0) && (4096 > 0))
                {.command = 0,      .size = 262144  },
              #endif
              #if (0 > 0)
                {.command = 0,       .size  = SPI_FLASH_ERASE_SIZE_CHIP_ERASE        },
              #endif
            };

            static const ospi_b_table_t g_ospi0_high_speed_erase_command_table = {
                .p_table = (void *)g_ospi0_high_speed_erase_command_list,
                .length = (uint8_t)(sizeof(g_ospi0_high_speed_erase_command_list) / sizeof(g_ospi0_high_speed_erase_command_list[0])),
            };
             #endif

            const ospi_b_xspi_command_set_t g_ospi0_high_speed_command_set =
            {
                .protocol             = SPI_FLASH_PROTOCOL_8D_8D_8D,
                .command_bytes        = OSPI_B_COMMAND_BYTES_2,
                .read_command         = 0xEEEE,
                .page_program_command = 0x1212,
                .write_enable_command = 0x0606,
                .status_command       = 0x0505,
                .read_dummy_cycles    = 20,
                .program_dummy_cycles = 0, /* Unused by OSPI Flash */
                .status_dummy_cycles  = 3,
             #if (0)
                .p_erase_commands     = &g_ospi0_high_speed_erase_command_table,
             #else
                .p_erase_commands = NULL, /* Use the erase commands specified in spi_flash_cfg_t */
             #endif
            };
            #else
            extern ospi_b_xspi_command_set_t [];
            #endif

            const ospi_b_table_t g_ospi0_command_set = {
            #if (0)
                .p_table = (void *),
                .length = 0,
            #else
                .p_table = (void *)&g_ospi0_high_speed_command_set,
                .length = 1,
            #endif
            };

            #if OSPI_B_CFG_DOTF_SUPPORT_ENABLE
            extern uint8_t g_ospi_dotf_iv[];
            extern uint8_t g_ospi_dotf_key[];

            static ospi_b_dotf_cfg_t g_ospi_dotf_cfg=
            {
                .key_type       = OSPI_B_DOTF_AES_KEY_TYPE_128,
                .format         = OSPI_B_DOTF_KEY_FORMAT_PLAINTEXT,
                .p_start_addr   = (uint32_t *)0x90000000,
                .p_end_addr     = (uint32_t *)0x90001FFF,
                .p_key          = (uint32_t *)g_ospi_dotf_key,
                .p_iv           = (uint32_t *)g_ospi_dotf_iv,
            };
            #endif

            static const ospi_b_extended_cfg_t g_ospi0_extended_cfg =
            {
                .ospi_b_unit                             = 0,
                .channel                                 = (ospi_b_device_number_t) 0,
                .data_latch_delay_clocks                 = 0x08,
                .p_timing_settings                       = &g_ospi0_timing_settings,
                .p_xspi_command_set                      = &g_ospi0_command_set,
                .p_autocalibration_preamble_pattern_addr = (uint8_t *) 0x00,
            #if OSPI_B_CFG_DMAC_SUPPORT_ENABLE
                .p_lower_lvl_transfer                    = &RA_NOT_DEFINED,
            #endif
            #if OSPI_B_CFG_DOTF_SUPPORT_ENABLE
                .p_dotf_cfg                              = &g_ospi_dotf_cfg,
            #endif
                .read_dummy_cycles                       = 0,
                .program_dummy_cycles                    = 0, /* Unused by OSPI Flash */
                .status_dummy_cycles                     = 0,
            };
            const spi_flash_cfg_t g_ospi0_cfg =
            {
                .spi_protocol                = SPI_FLASH_PROTOCOL_8D_8D_8D,
                .read_mode                   = SPI_FLASH_READ_MODE_STANDARD, /* Unused by OSPI Flash */
                .address_bytes               = SPI_FLASH_ADDRESS_BYTES_4,
                .dummy_clocks                = SPI_FLASH_DUMMY_CLOCKS_DEFAULT, /* Unused by OSPI Flash */
                .page_program_address_lines  = (spi_flash_data_lines_t) 0U, /* Unused by OSPI Flash */
                .page_size_bytes             = 64,
                .write_status_bit            = 0,
                .write_enable_bit            = 1,
                .page_program_command        = 0x12,
                .write_enable_command        = 0x06,
                .status_command              = 0x05,
                .read_command                = 0x13,
            #if OSPI_B_CFG_XIP_SUPPORT_ENABLE
                .xip_enter_command           = 0,
                .xip_exit_command            = 0,
            #else
                .xip_enter_command           = 0U,
                .xip_exit_command            = 0U,
            #endif
                .erase_command_list_length   = sizeof(g_ospi0_erase_command_list) / sizeof(g_ospi0_erase_command_list[0]),
                .p_erase_command_list        = &g_ospi0_erase_command_list[0],
                .p_extend                    = &g_ospi0_extended_cfg,
            };
            /** This structure encompasses everything that is needed to use an instance of this interface. */
            const spi_flash_instance_t g_ospi0 =
            {
                .p_ctrl = &g_ospi0_ctrl,
                .p_cfg =  &g_ospi0_cfg,
                .p_api =  &g_ospi_b_on_spi_flash,
            };

            #if defined OSPI_B_CFG_DOTF_PROTECTED_MODE_SUPPORT_ENABLE
            rsip_instance_t const * const gp_rsip_instance = &RA_NOT_DEFINED;
            #endif
iic_master_instance_ctrl_t g_i2c_master1_ctrl;
const iic_master_extended_cfg_t g_i2c_master1_extend =
{
    .timeout_mode             = IIC_MASTER_TIMEOUT_MODE_SHORT,
    .timeout_scl_low          = IIC_MASTER_TIMEOUT_SCL_LOW_ENABLED,
    .smbus_operation         = 0,
    /* Actual calculated bitrate: 98945. Actual calculated duty cycle: 51%. */ .clock_settings.brl_value = 15, .clock_settings.brh_value = 16, .clock_settings.cks_value = 4, .clock_settings.sddl_value = 0, .clock_settings.dlcs_value = 0,
};
const i2c_master_cfg_t g_i2c_master1_cfg =
{
    .channel             = 1,
    .rate                = I2C_MASTER_RATE_STANDARD,
    .slave               = 0x3c,
    .addr_mode           = I2C_MASTER_ADDR_MODE_7BIT,
#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
                .p_transfer_tx       = NULL,
#else
                .p_transfer_tx       = &RA_NOT_DEFINED,
#endif
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
                .p_transfer_rx       = NULL,
#else
                .p_transfer_rx       = &RA_NOT_DEFINED,
#endif
#undef RA_NOT_DEFINED
    .p_callback          = g_i2c_callback,
    .p_context           = NULL,
#if defined(VECTOR_NUMBER_IIC1_RXI)
    .rxi_irq             = VECTOR_NUMBER_IIC1_RXI,
#else
    .rxi_irq             = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_IIC1_TXI)
    .txi_irq             = VECTOR_NUMBER_IIC1_TXI,
#else
    .txi_irq             = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_IIC1_TEI)
    .tei_irq             = VECTOR_NUMBER_IIC1_TEI,
#else
    .tei_irq             = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_IIC1_ERI)
    .eri_irq             = VECTOR_NUMBER_IIC1_ERI,
#else
    .eri_irq             = FSP_INVALID_VECTOR,
#endif
    .ipl                 = (2),
    .p_extend            = &g_i2c_master1_extend,
};
/* Instance structure to use this module. */
const i2c_master_instance_t g_i2c_master1 =
{
    .p_ctrl        = &g_i2c_master1_ctrl,
    .p_cfg         = &g_i2c_master1_cfg,
    .p_api         = &g_i2c_master_on_iic
};
ceu_instance_ctrl_t g_ceu0_ctrl;
            const ceu_extended_cfg_t g_ceu0_extended_cfg =
            {
                .capture_format       = CEU_CAPTURE_FORMAT_DATA_SYNCHRONOUS,
                .data_bus_width       = CEU_DATA_BUS_SIZE_8_BIT,
                .edge_info.dsel       = 0,
                .edge_info.hdsel      = 0,
                .edge_info.vdsel      = 0,
                .hsync_polarity       = CEU_HSYNC_POLARITY_HIGH,
                .vsync_polarity       = CEU_VSYNC_POLARITY_HIGH,
                .byte_swapping        = {
                                        .swap_8bit_units  = ( 0x0) >> 0x00 & 0x01,
                                        .swap_16bit_units = ( 0x0) >> 0x01 & 0x01,
                                        .swap_32bit_units = ( 0x0) >> 0x02 & 0x01,
                                        },
                .burst_mode           = CEU_BURST_TRANSFER_MODE_X1,
                .image_area_size      = 320 * 240 * 2,
                .interrupts_enabled   = 0 | \
                                        R_CEU_CEIER_CPEIE_Msk | \
                                        0 | \
                                        R_CEU_CEIER_VDIE_Msk | \
                                        R_CEU_CEIER_CDTOFIE_Msk | \
                                        0 | \
                                        0 | \
                                        R_CEU_CEIER_VBPIE_Msk | \
                                        R_CEU_CEIER_NHDIE_Msk | \
                                        R_CEU_CEIER_NVDIE_Msk,
                .ceu_ipl              = (2),
                .ceu_irq              = VECTOR_NUMBER_CEU_CEUI,
            };

            const capture_cfg_t g_ceu0_cfg =
            {
                .x_capture_pixels      = 320,
                .y_capture_pixels      = 240,
                .x_capture_start_pixel = 0,
                .y_capture_start_pixel = 0,
                .bytes_per_pixel       = 2,
                .p_callback            = g_ceu0_user_callback,
                .p_context             = NULL,
                .p_extend              = &g_ceu0_extended_cfg,
            };

            const capture_instance_t g_ceu0 =
            {
                .p_ctrl = &g_ceu0_ctrl,
                .p_cfg =  &g_ceu0_cfg,
                .p_api =  &g_ceu_on_capture,
            };
sci_b_uart_instance_ctrl_t     g_uart9_ctrl;

            sci_b_baud_setting_t               g_uart9_baud_setting =
            {
                /* Baud rate calculated with 3.340% error. */ .baudrate_bits_b.abcse = 1, .baudrate_bits_b.abcs = 0, .baudrate_bits_b.bgdm = 0, .baudrate_bits_b.cks = 0, .baudrate_bits_b.brr = 6, .baudrate_bits_b.mddr = (uint8_t) 256, .baudrate_bits_b.brme = false
            };

            /** UART extended configuration for UARTonSCI HAL driver */
            const sci_b_uart_extended_cfg_t g_uart9_cfg_extend =
            {
                .clock                = SCI_B_UART_CLOCK_INT,
                .rx_edge_start          = SCI_B_UART_START_BIT_FALLING_EDGE,
                .noise_cancel         = SCI_B_UART_NOISE_CANCELLATION_ENABLE,
                .rx_fifo_trigger        = SCI_B_UART_RX_FIFO_TRIGGER_MAX,
                .p_baud_setting         = &g_uart9_baud_setting,
                .flow_control           = SCI_B_UART_FLOW_CONTROL_RTS,
                #if 0xFF != 0xFF
                .flow_control_pin       = BSP_IO_PORT_FF_PIN_0xFF,
                #else
                .flow_control_pin       = (bsp_io_port_pin_t) UINT16_MAX,
                #endif
                .rs485_setting          = {
                    .enable = SCI_B_UART_RS485_DISABLE,
                    .polarity = SCI_B_UART_RS485_DE_POLARITY_HIGH,
                    .assertion_time = 1,
                    .negation_time = 1,
                }
            };

            /** UART interface configuration */
            const uart_cfg_t g_uart9_cfg =
            {
                .channel             = 9,
                .data_bits           = UART_DATA_BITS_8,
                .parity              = UART_PARITY_OFF,
                .stop_bits           = UART_STOP_BITS_1,
                .p_callback          = NULL,
                .p_context           = NULL,
                .p_extend            = &g_uart9_cfg_extend,
#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
                .p_transfer_tx       = NULL,
#else
                .p_transfer_tx       = &RA_NOT_DEFINED,
#endif
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
                .p_transfer_rx       = NULL,
#else
                .p_transfer_rx       = &RA_NOT_DEFINED,
#endif
#undef RA_NOT_DEFINED
                .rxi_ipl             = (12),
                .txi_ipl             = (12),
                .tei_ipl             = (12),
                .eri_ipl             = (12),
#if defined(VECTOR_NUMBER_SCI9_RXI)
                .rxi_irq             = VECTOR_NUMBER_SCI9_RXI,
#else
                .rxi_irq             = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SCI9_TXI)
                .txi_irq             = VECTOR_NUMBER_SCI9_TXI,
#else
                .txi_irq             = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SCI9_TEI)
                .tei_irq             = VECTOR_NUMBER_SCI9_TEI,
#else
                .tei_irq             = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SCI9_ERI)
                .eri_irq             = VECTOR_NUMBER_SCI9_ERI,
#else
                .eri_irq             = FSP_INVALID_VECTOR,
#endif
            };

/* Instance structure to use this module. */
const uart_instance_t g_uart9 =
{
    .p_ctrl        = &g_uart9_ctrl,
    .p_cfg         = &g_uart9_cfg,
    .p_api         = &g_uart_on_sci_b
};
void g_hal_init(void) {
g_common_init();
}
