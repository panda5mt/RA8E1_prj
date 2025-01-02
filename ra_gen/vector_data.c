/* generated vector source file - do not edit */
#include "bsp_api.h"
/* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
#if VECTOR_DATA_IRQ_COUNT > 0
BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
    {
        [0] = sci_b_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
        [1] = sci_b_uart_txi_isr, /* SCI9 TXI (Transmit data empty) */
        [2] = sci_b_uart_tei_isr, /* SCI9 TEI (Transmit end) */
        [3] = sci_b_uart_eri_isr, /* SCI9 ERI (Receive error) */
        [4] = ceu_isr,            /* CEU CEUI (CEU interrupt) */
        [5] = iic_master_rxi_isr, /* IIC1 RXI (Receive data full) */
        [6] = iic_master_txi_isr, /* IIC1 TXI (Transmit data empty) */
        [7] = iic_master_tei_isr, /* IIC1 TEI (Transmit end) */
        [8] = iic_master_eri_isr, /* IIC1 ERI (Transfer error) */
};
#if BSP_FEATURE_ICU_HAS_IELSR
const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] =
    {
        [0] = BSP_PRV_VECT_ENUM(EVENT_SCI9_RXI, GROUP0), /* SCI9 RXI (Receive data full) */
        [1] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TXI, GROUP1), /* SCI9 TXI (Transmit data empty) */
        [2] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TEI, GROUP2), /* SCI9 TEI (Transmit end) */
        [3] = BSP_PRV_VECT_ENUM(EVENT_SCI9_ERI, GROUP3), /* SCI9 ERI (Receive error) */
        [4] = BSP_PRV_VECT_ENUM(EVENT_CEU_CEUI, GROUP4), /* CEU CEUI (CEU interrupt) */
        [5] = BSP_PRV_VECT_ENUM(EVENT_IIC1_RXI, GROUP5), /* IIC1 RXI (Receive data full) */
        [6] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TXI, GROUP6), /* IIC1 TXI (Transmit data empty) */
        [7] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TEI, GROUP7), /* IIC1 TEI (Transmit end) */
        [8] = BSP_PRV_VECT_ENUM(EVENT_IIC1_ERI, GROUP0), /* IIC1 ERI (Transfer error) */
};
#endif
#endif