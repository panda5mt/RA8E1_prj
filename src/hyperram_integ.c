#include "hyperram_integ.h"
#include "putchar_ra8usb.h"
#include "hal_data.h"

#include "r_ospi_b.h"
#include "r_spi_flash_api.h"
#include <string.h>
/* Flash device timing */
#define OSPI_B_TIME_UNIT (BSP_DELAY_UNITS_MICROSECONDS)
#define OSPI_B_TIME_RESET_SETUP (2U)    /*  Type 50ns */
#define OSPI_B_TIME_RESET_PULSE (1000U) /*  Type 500us */

spi_flash_direct_transfer_t g_ospi0_trans;
bool ospi_b_dma_sent = false;
/* Custom command sets. */
ospi_b_xspi_command_set_t g_command_sets[] =
    {

        /* 8D-8D-8D example with inverted lower command byte. */
        [0] = {.protocol = SPI_FLASH_PROTOCOL_8D_8D_8D, //
               .latency_mode = OSPI_B_LATENCY_MODE_VARIABLE,
               .frame_format = OSPI_B_FRAME_FORMAT_XSPI_PROFILE_1,
               .command_bytes = OSPI_RAM_COMMAND_BYTES,
               .address_bytes = SPI_FLASH_ADDRESS_BYTES_4,
               .read_command = OSPI_B_COMMAND_READ,
               .program_command = OSPI_B_COMMAND_WRITE,
               .write_enable_command = OSPI_B_COMMAND_WRITE_ENABLE,
               .status_command = 0x00,
               //.status_needs_address = false,
               //.status_address_bytes = 0,
               .address_msb_mask = 0xE0,
               .read_dummy_cycles = OSPI_RAM_LATENCY_CYCLES,
               .program_dummy_cycles = OSPI_RAM_LATENCY_CYCLES,

               .status_dummy_cycles = NULL,
               .p_erase_commands = NULL}};

fsp_err_t ospi_raw_trans(spi_flash_direct_transfer_t *p_trans,
                         uint16_t command, uint8_t cmd_len,
                         uint32_t address, uint8_t addr_len,
                         uint32_t data, uint8_t data_len,
                         uint8_t dummy_cycle, spi_flash_direct_transfer_dir_t dir)
{
    fsp_err_t err = FSP_SUCCESS;

    // Example raw transfer
    p_trans->command = command;
    p_trans->command_length = cmd_len;
    p_trans->address = address;
    p_trans->address_length = addr_len;
    p_trans->data_length = data_len;
    p_trans->data = data;
    p_trans->dummy_cycles = dummy_cycle;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, p_trans, dir);
    return err;
}

// ★ OSPI レジスタの要点ダンプ（ch=0/1 の両方見ます）
void ospi_dump_regs(void)
{
    R_XSPI0_Type *p = R_XSPI0;

    for (int ch = 0; ch < 2; ch++)
    {
        xprintf("\n[OSPI] ---- CH%d ----\n", ch);
        xprintf("LIOCFGCS[%d] = 0x%08X\n", ch, p->LIOCFGCS[ch]);
        xprintf("CMCFGCS[%d].CMCFG0= 0x%08X\n", ch, p->CMCFGCS[ch].CMCFG0);
        xprintf("CMCFGCS[%d].CMCFG1= 0x%08X\n", ch, p->CMCFGCS[ch].CMCFG1); // RDCMD/RDLATE
        xprintf("CMCFGCS[%d].CMCFG2= 0x%08X\n", ch, p->CMCFGCS[ch].CMCFG2); // WRCMD/WRLATE
    }

    xprintf("\n[OSPI] BMCTL0=0x%08X,BMCTL1=0x%08X,WRAPCFG=0x%08X\n",
            R_XSPI0->BMCTL0, R_XSPI0->BMCTL1, R_XSPI0->WRAPCFG);
    xprintf("\n[OSPI] COMSTT=0x%08X\n", R_XSPI0->COMSTT);
    xprintf("[OSPI] BMCFGCH[0]=0x%08X,BMCFGCH[1]=0x%08X\n",
            R_XSPI0->BMCFGCH[0], R_XSPI0->BMCFGCH[1]);
}

void ospi_wait_mmap_idle(void)
{
    const uint32_t BUSY_MASK = (0x03u << R_XSPI0_COMSTT_MEMACCCH_Pos);
    while (R_XSPI0->COMSTT & BUSY_MASK)
    {
        __NOP();
    }
}

void dump_ospi_read_side(R_XSPI0_Type *r, int ch)
{
    uint32_t cm0 = r->CMCFGCS[ch].CMCFG0;
    uint32_t cm1 = r->CMCFGCS[ch].CMCFG1;
    uint32_t liocfg = r->LIOCFGCS[ch];
    uint32_t wrap = r->WRAPCFG;

    uint32_t rdcmd = (cm1 >> R_XSPI0_CMCFGCS_CMCFG1_RDCMD_Pos) & 0xFFFF;
    uint32_t rdlate = (cm1 >> R_XSPI0_CMCFGCS_CMCFG1_RDLATE_Pos) & 0xFF;
    uint32_t addsz = (cm0 >> R_XSPI0_CMCFGCS_CMCFG0_ADDSIZE_Pos) & 0x3;
    uint32_t ffmt = (cm0 >> R_XSPI0_CMCFGCS_CMCFG0_FFMT_Pos) & 0x7;
    uint32_t latemd = (liocfg >> R_XSPI0_LIOCFGCS_LATEMD_Pos) & 0x1;
    uint32_t dssft0 = (wrap >> R_XSPI0_WRAPCFG_DSSFTCS0_Pos) & 0x1F;
    uint32_t dssft1 = (wrap >> R_XSPI0_WRAPCFG_DSSFTCS1_Pos) & 0x1F;

    xprintf("RDCMD=0x%04x RDLATE=%u ADDRSIZE=%u FFMT=%u\n",
            (unsigned long)rdcmd, (unsigned long)rdlate, (unsigned long)addsz,
            (unsigned long)ffmt);

    xprintf("LATEMD=%u DSSFT0=%u DSSFT1=%u\n",
            (unsigned long)latemd, (unsigned long)dssft0, (unsigned long)dssft1);

    uint32_t cmcfg0 = r->CMCFGCS[ch].CMCFG0;
    bool addr_replace_enabled =
        (cmcfg0 & R_XSPI0_CMCFGCS_CMCFG0_ADDRPEN_Msk) != 0;

    xprintf("CMCFG0[%d] = 0x%08X\n", ch, (unsigned long)cmcfg0);
    xprintf("ADDRPEN (Address Replace) = %s\n",
            addr_replace_enabled ? "ENABLED" : "DISABLED");
}

fsp_err_t hyperram_init(void)
{
    fsp_err_t err = FSP_SUCCESS;
    R_BSP_MODULE_START(FSP_IP_OSPI, 0);
    // 0. VCCによるHW設定 VCC2 = 1.8VなのでLVOCR.LVO1E=1にする
    uint32_t *lvocr_ptr = (uint32_t *)0x4001E000;
    xprintf("[SYSTEM] LVOCR = 0x%02x\n", *lvocr_ptr);
    *lvocr_ptr = 0x0001;
    xprintf("[SYSTEM] LVOCR = 0x%02x\n", *lvocr_ptr);

    // 1. OSPI 初期化
    /* Reset flash device by driving OM_RESET pin */
    // R_XSPI0->LIOCTL_b.RSTCS0 = 0;
    // R_BSP_SoftwareDelay(OSPI_B_TIME_RESET_PULSE, OSPI_B_TIME_UNIT);
    // R_XSPI0->LIOCTL_b.RSTCS0 = 1;
    // R_BSP_SoftwareDelay(OSPI_B_TIME_RESET_SETUP, OSPI_B_TIME_UNIT);
    SCB_InvalidateDCache_by_Addr((uint8_t *)HYPERRAM_BASE_ADDR, 256 * 256 * 2);

    err = R_OSPI_B_Open(&g_ospi0_ctrl, &g_ospi0_cfg);
    if (FSP_SUCCESS != err)
    {
        //__BKPT(); // 初期化失敗
        xprintf("[OSPI] init error!%d\n", err);
        return err;
    }

    err = R_OSPI_B_SpiProtocolSet(&g_ospi0_ctrl, SPI_FLASH_PROTOCOL_8D_8D_8D);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] set protocol error!:%d\n", err);
        return err;
    }

    xprintf("[OSPI] init Ok\n");

    R_XSPI0->WRAPCFG_b.DSSFTCS1 = 16;

    // /* Configure DDR sampling window extend */
    R_XSPI0->LIOCFGCS_b[1].DDRSMPEX = 10;
    // default CR = 0x52F0(LE) -> 0xF052(BE) (Normal Operation, 24ohm, no DQSM pre-cycle, 8-clock latency, variable latency, 32bytes burst)
    // write CR = 0xC052(BE) -> 0x52C0(LE) (Normal Operation, 34ohm, no DQSM pre-cycle, 8-clock latency, variable latency, 32bytes burst)
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE_REGISTER, OSPI_RAM_COMMAND_BYTES,
                         0x00040000, 4,
                         0x52C0, 2, // 64Byte burst, Latency 7
                         0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }

    // read ID
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ_ID, OSPI_RAM_COMMAND_BYTES,
                         0x00000000, 4,
                         0x00, 2,
                         OSPI_RAM_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }
    xprintf("ID=0x%04x\n", g_ospi0_trans.data);

    // read CR
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ_REGISTER, OSPI_RAM_COMMAND_BYTES,
                         0x00040000, 4,
                         0x00, 2,
                         OSPI_RAM_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }
    xprintf("CR=0x%04x\n", g_ospi0_trans.data);

    uint32_t arr32[8] = {0xdeadbeef,
                         0x11223344, 0x55667788, 0x99AABBCC,
                         0xDDEEFF00,
                         0x12345678, 0x9ABCDEF0, 0x0FEDCBA9};

    for (int i = 0; i < 8; i++)
    {
        int adr = i * 4;
        adr = ((adr & 0xfffffff0) << 6) | (adr & 0x0f); // Octal ram address format
        // xprintf("Write 0x%08X \n", adr);
        err = ospi_raw_trans(&g_ospi0_trans,
                             OSPI_B_COMMAND_WRITE, OSPI_RAM_COMMAND_BYTES,
                             adr, 4,
                             arr32[i], 4,
                             OSPI_RAM_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
        if (FSP_SUCCESS != err)
        {
            xprintf("[OSPI] direct transfer error!\n");
        }
    }

    for (int i = 0; i < 8; i++)
    {
        int adr = i * 4;
        adr = ((adr & 0xfffffff0) << 6) | (adr & 0x0f); // Octal ram address format
        err = ospi_raw_trans(&g_ospi0_trans,
                             OSPI_B_COMMAND_READ, OSPI_RAM_COMMAND_BYTES,
                             adr, 4,
                             0x00, 4,
                             OSPI_RAM_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
        if (FSP_SUCCESS != err)
        {
            xprintf("[OSPI] direct transfer error!\n");
        }
        xprintf("0x%08X\n", g_ospi0_trans.data);
    }

    for (int i = 0; i < 8; i++)
    {
        int adr = i * 4;
        adr = ((adr & 0xfffffff0) << 6) | (adr & 0x0f); // Octal ram address format
        xprintf("mapped read[%d]=0x%08X\n", i, *((volatile uint32_t *)((uint8_t *)HYPERRAM_BASE_ADDR + adr)));
    }

    // while (1)
    //     ;

    // err = ospi_raw_trans(&g_ospi0_trans,
    //                      OSPI_B_COMMAND_WRITE, OSPI_RAM_COMMAND_BYTES,
    //                      0x00, 4,
    //                      0xFFFF0000, 4, // CK+,CK-
    //                      OSPI_RAM_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    // if (FSP_SUCCESS != err)
    // {
    //     xprintf("[OSPI] direct transfer error!\n");
    //     return err;
    // }

    // err = ospi_raw_trans(&g_ospi0_trans,
    //                      OSPI_B_COMMAND_WRITE, OSPI_RAM_COMMAND_BYTES,
    //                      0x04, 4,
    //                      0x000800FF, 4, // CK+,CK-
    //                      OSPI_RAM_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    // if (FSP_SUCCESS != err)
    // {
    //     xprintf("[OSPI] direct transfer error!\n");
    //     return err;
    // }

    // err = ospi_raw_trans(&g_ospi0_trans,
    //                      OSPI_B_COMMAND_WRITE, OSPI_RAM_COMMAND_BYTES,
    //                      0x08, 4,
    //                      0x00FFF700U, 4, // CK+,CK-
    //                      OSPI_RAM_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    // if (FSP_SUCCESS != err)
    // {
    //     xprintf("[OSPI] direct transfer error!\n");
    //     return err;
    // }

    // err = ospi_raw_trans(&g_ospi0_trans,
    //                      OSPI_B_COMMAND_WRITE, OSPI_RAM_COMMAND_BYTES,
    //                      0x0C, 4,
    //                      0xF700F708, 4, // CK+,CK-
    //                      OSPI_RAM_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    // if (FSP_SUCCESS != err)
    // {
    //     xprintf("[OSPI] direct transfer error!\n");
    //     return err;
    // }
    // /////
    // err = R_OSPI_B_AutoCalibrate(&g_ospi0_ctrl);
    // if (FSP_SUCCESS != err)
    // {
    //     xprintf("[OSPI] AutoCalib error!%d\n", err);
    //     return err;
    // }
    // // AutoCal 実行後（FSP: R_OSPI_B_Open 内で data_latch_delay_clocks==0 の時に自動実行）
    // uint32_t wrap = R_XSPI0->WRAPCFG;
    // // CS1 用（0x9000_0000 側）
    // uint32_t dssft_cs1 = (wrap & R_XSPI0_WRAPCFG_DSSFTCS1_Msk) >> R_XSPI0_WRAPCFG_DSSFTCS1_Pos;

    // xprintf("[AutoCal result] wrap=%d,DSSFT CS1=%d\n", wrap, dssft_cs1);

    // xprintf("DSSFT CS1=%d\n", R_XSPI0->WRAPCFG_b.DSSFTCS1);

    // 正常終了
    xprintf("[OSPI] RW init end\n");

    return err;
}

fsp_err_t hyperram_b_write(const void *p_src, void *p_dest, uint32_t total_length)
{
    fsp_err_t err = FSP_SUCCESS;

    const uint8_t *src_p8 = (const uint8_t *)p_src;
    const uint32_t *src_p32 = (const uint32_t *)p_src;

    uint8_t *dest_p8 = (uint8_t *)p_dest;
    uint32_t *dest_p32 = (uint32_t *)p_dest;

    // write
    for (uint32_t z = 0; z < total_length / 4; z++)
    {
        uint32_t data = src_p32[z];
        uint32_t adr = (dest_p8) + z * 4;               // 8bit length address
        adr = ((adr & 0xfffffff0) << 6) | (adr & 0x0f); // Octal ram address format

        err = ospi_raw_trans(&g_ospi0_trans,
                             OSPI_B_COMMAND_WRITE, OSPI_RAM_COMMAND_BYTES,
                             adr, 4,
                             data, 4,
                             OSPI_RAM_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
        if (FSP_SUCCESS != err)
        {
            xprintf("[OSPI] direct transfer error!\n");
        }
    }
    return err;
}

void ospi_dmac_cb(transfer_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    ospi_b_dma_sent = true;
    // xprintf("OSPI DMAC transfer done.\n");
}