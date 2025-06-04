/* generated vector header file - do not edit */
        #ifndef VECTOR_DATA_H
        #define VECTOR_DATA_H
        #ifdef __cplusplus
        extern "C" {
        #endif
                /* Number of interrupts allocated */
        #ifndef VECTOR_DATA_IRQ_COUNT
        #define VECTOR_DATA_IRQ_COUNT    (10)
        #endif
        /* ISR prototypes */
        void ceu_isr(void);
        void iic_master_rxi_isr(void);
        void iic_master_txi_isr(void);
        void iic_master_tei_isr(void);
        void iic_master_eri_isr(void);
        void usbfs_interrupt_handler(void);
        void usbfs_resume_handler(void);
        void usbfs_d0fifo_handler(void);
        void usbfs_d1fifo_handler(void);
        void ether_eint_isr(void);

        /* Vector table allocations */
        #define VECTOR_NUMBER_CEU_CEUI ((IRQn_Type) 0) /* CEU CEUI (CEU interrupt) */
        #define CEU_CEUI_IRQn          ((IRQn_Type) 0) /* CEU CEUI (CEU interrupt) */
        #define VECTOR_NUMBER_IIC1_RXI ((IRQn_Type) 1) /* IIC1 RXI (Receive data full) */
        #define IIC1_RXI_IRQn          ((IRQn_Type) 1) /* IIC1 RXI (Receive data full) */
        #define VECTOR_NUMBER_IIC1_TXI ((IRQn_Type) 2) /* IIC1 TXI (Transmit data empty) */
        #define IIC1_TXI_IRQn          ((IRQn_Type) 2) /* IIC1 TXI (Transmit data empty) */
        #define VECTOR_NUMBER_IIC1_TEI ((IRQn_Type) 3) /* IIC1 TEI (Transmit end) */
        #define IIC1_TEI_IRQn          ((IRQn_Type) 3) /* IIC1 TEI (Transmit end) */
        #define VECTOR_NUMBER_IIC1_ERI ((IRQn_Type) 4) /* IIC1 ERI (Transfer error) */
        #define IIC1_ERI_IRQn          ((IRQn_Type) 4) /* IIC1 ERI (Transfer error) */
        #define VECTOR_NUMBER_USBFS_INT ((IRQn_Type) 5) /* USBFS INT (USBFS interrupt) */
        #define USBFS_INT_IRQn          ((IRQn_Type) 5) /* USBFS INT (USBFS interrupt) */
        #define VECTOR_NUMBER_USBFS_RESUME ((IRQn_Type) 6) /* USBFS RESUME (USBFS resume interrupt) */
        #define USBFS_RESUME_IRQn          ((IRQn_Type) 6) /* USBFS RESUME (USBFS resume interrupt) */
        #define VECTOR_NUMBER_USBFS_FIFO_0 ((IRQn_Type) 7) /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
        #define USBFS_FIFO_0_IRQn          ((IRQn_Type) 7) /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
        #define VECTOR_NUMBER_USBFS_FIFO_1 ((IRQn_Type) 8) /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
        #define USBFS_FIFO_1_IRQn          ((IRQn_Type) 8) /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
        #define VECTOR_NUMBER_EDMAC0_EINT ((IRQn_Type) 9) /* EDMAC0 EINT (EDMAC 0 interrupt) */
        #define EDMAC0_EINT_IRQn          ((IRQn_Type) 9) /* EDMAC0 EINT (EDMAC 0 interrupt) */
        /* The number of entries required for the ICU vector table. */
        #define BSP_ICU_VECTOR_NUM_ENTRIES (10)

        #ifdef __cplusplus
        }
        #endif
        #endif /* VECTOR_DATA_H */