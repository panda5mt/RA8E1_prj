/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = ceu_isr, /* CEU CEUI (CEU interrupt) */
            [1] = iic_master_rxi_isr, /* IIC1 RXI (Receive data full) */
            [2] = iic_master_txi_isr, /* IIC1 TXI (Transmit data empty) */
            [3] = iic_master_tei_isr, /* IIC1 TEI (Transmit end) */
            [4] = iic_master_eri_isr, /* IIC1 ERI (Transfer error) */
            [5] = dmac_int_isr, /* DMAC1 INT (DMAC1 transfer end) */
            [6] = usbfs_interrupt_handler, /* USBFS INT (USBFS interrupt) */
            [7] = usbfs_resume_handler, /* USBFS RESUME (USBFS resume interrupt) */
            [8] = usbfs_d0fifo_handler, /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [9] = usbfs_d1fifo_handler, /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
            [10] = ether_eint_isr, /* EDMAC0 EINT (EDMAC 0 interrupt) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_CEU_CEUI,GROUP0), /* CEU CEUI (CEU interrupt) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_IIC1_RXI,GROUP1), /* IIC1 RXI (Receive data full) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TXI,GROUP2), /* IIC1 TXI (Transmit data empty) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TEI,GROUP3), /* IIC1 TEI (Transmit end) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_IIC1_ERI,GROUP4), /* IIC1 ERI (Transfer error) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_DMAC1_INT,GROUP5), /* DMAC1 INT (DMAC1 transfer end) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_USBFS_INT,GROUP6), /* USBFS INT (USBFS interrupt) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_USBFS_RESUME,GROUP7), /* USBFS RESUME (USBFS resume interrupt) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_0,GROUP0), /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_1,GROUP1), /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_EDMAC0_EINT,GROUP2), /* EDMAC0 EINT (EDMAC 0 interrupt) */
        };
        #endif
        #endif