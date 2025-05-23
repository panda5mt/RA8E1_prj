/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = sci_b_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
            [1] = sci_b_uart_txi_isr, /* SCI9 TXI (Transmit data empty) */
            [2] = sci_b_uart_tei_isr, /* SCI9 TEI (Transmit end) */
            [3] = sci_b_uart_eri_isr, /* SCI9 ERI (Receive error) */
            [4] = ceu_isr, /* CEU CEUI (CEU interrupt) */
            [5] = iic_master_rxi_isr, /* IIC1 RXI (Receive data full) */
            [6] = iic_master_txi_isr, /* IIC1 TXI (Transmit data empty) */
            [7] = iic_master_tei_isr, /* IIC1 TEI (Transmit end) */
            [8] = iic_master_eri_isr, /* IIC1 ERI (Transfer error) */
            [9] = ether_eint_isr, /* EDMAC0 EINT (EDMAC 0 interrupt) */
            [10] = usbfs_interrupt_handler, /* USBFS INT (USBFS interrupt) */
            [11] = usbfs_resume_handler, /* USBFS RESUME (USBFS resume interrupt) */
            [12] = usbfs_d0fifo_handler, /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [13] = usbfs_d1fifo_handler, /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_SCI9_RXI,GROUP0), /* SCI9 RXI (Receive data full) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TXI,GROUP1), /* SCI9 TXI (Transmit data empty) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TEI,GROUP2), /* SCI9 TEI (Transmit end) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_SCI9_ERI,GROUP3), /* SCI9 ERI (Receive error) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_CEU_CEUI,GROUP4), /* CEU CEUI (CEU interrupt) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_IIC1_RXI,GROUP5), /* IIC1 RXI (Receive data full) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TXI,GROUP6), /* IIC1 TXI (Transmit data empty) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TEI,GROUP7), /* IIC1 TEI (Transmit end) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_IIC1_ERI,GROUP0), /* IIC1 ERI (Transfer error) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_EDMAC0_EINT,GROUP1), /* EDMAC0 EINT (EDMAC 0 interrupt) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_USBFS_INT,GROUP2), /* USBFS INT (USBFS interrupt) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_USBFS_RESUME,GROUP3), /* USBFS RESUME (USBFS resume interrupt) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_0,GROUP4), /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [13] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_1,GROUP5), /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
        };
        #endif
        #endif