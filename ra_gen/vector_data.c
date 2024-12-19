/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = ceu_isr, /* CEU CEUI (CEU interrupt) */
            [1] = iic_master_rxi_isr, /* IIC0 RXI (Receive data full) */
            [2] = iic_master_txi_isr, /* IIC0 TXI (Transmit data empty) */
            [3] = iic_master_tei_isr, /* IIC0 TEI (Transmit end) */
            [4] = iic_master_eri_isr, /* IIC0 ERI (Transfer error) */
            [5] = sci_b_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
            [6] = sci_b_uart_txi_isr, /* SCI9 TXI (Transmit data empty) */
            [7] = sci_b_uart_tei_isr, /* SCI9 TEI (Transmit end) */
            [8] = sci_b_uart_eri_isr, /* SCI9 ERI (Receive error) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_CEU_CEUI,GROUP0), /* CEU CEUI (CEU interrupt) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_IIC0_RXI,GROUP1), /* IIC0 RXI (Receive data full) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_IIC0_TXI,GROUP2), /* IIC0 TXI (Transmit data empty) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_IIC0_TEI,GROUP3), /* IIC0 TEI (Transmit end) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_IIC0_ERI,GROUP4), /* IIC0 ERI (Transfer error) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_SCI9_RXI,GROUP5), /* SCI9 RXI (Receive data full) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TXI,GROUP6), /* SCI9 TXI (Transmit data empty) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TEI,GROUP7), /* SCI9 TEI (Transmit end) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_SCI9_ERI,GROUP0), /* SCI9 ERI (Receive error) */
        };
        #endif
        #endif