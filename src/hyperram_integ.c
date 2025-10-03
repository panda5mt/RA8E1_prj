#include "hyperram_integ.h"
#include "putchar_ra8usb.h"
#include "hal_data.h"

#include "r_ospi_b.h"
#include "r_spi_flash_api.h"
#include <string.h>

// #define HYPERRAM_BASE_ADDR ((void *)0x90000000U) /* Device on CS1 */
spi_flash_direct_transfer_t g_ospi0_trans;

/* Custom command sets. */
ospi_b_xspi_command_set_t g_command_sets[] =
    {
        /* 8D-8D-8D example with inverted lower command byte. */
        [0] = {
            .protocol = SPI_FLASH_PROTOCOL_8D_8D_8D,
            .latency_mode = OSPI_B_LATENCY_MODE_FIXED,
            .frame_format = OSPI_B_FRAME_FORMAT_XSPI_PROFILE_2,
            .command_bytes = OSPI_B_COMMAND_BYTES_2,
            .address_bytes = SPI_FLASH_ADDRESS_BYTES_4,
            .read_command = OSPI_B_COMMAND_READ,
            .program_command = OSPI_B_COMMAND_WRITE,
            .write_enable_command = OSPI_B_COMMAND_WRITE_ENABLE,
            .status_command = NULL,
            .address_msb_mask = 0xf0,
            .read_dummy_cycles = 15U,
            .program_dummy_cycles = 15U,

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

fsp_err_t hyperram_init(void)
{
    fsp_err_t err = FSP_SUCCESS;

    // 0. VCCによるHW設定 VCC2 = 1.8VなのでLVOCR.LVO1E=1にする
    uint32_t *lvocr_ptr = (uint32_t *)0x4001E000;
    xprintf("[SYSTEM] LVOCR = 0x%02x\n", *lvocr_ptr);
    *lvocr_ptr = 0x0001;
    xprintf("[SYSTEM] LVOCR = 0x%02x\n", *lvocr_ptr);

    // 1. OSPI 初期化
    err = R_OSPI_B_Open(&g_ospi0_ctrl, &g_ospi0_cfg);
    if (FSP_SUCCESS != err)
    {
        //__BKPT(); // 初期化失敗
        xprintf("[OSPI] init error!\n");
        return err;
    }
    // err = R_OSPI_B_SpiProtocolSet(&g_ospi0_ctrl, SPI_FLASH_PROTOCOL_8D_8D_8D);
    // if (FSP_SUCCESS != err)
    // {
    //     xprintf("[OSPI] set protocol error!:%d\n", err);
    //     return err;
    // }

    xprintf("[OSPI] init Ok\n");

    int wrap = 4;
    R_XSPI0->WRAPCFG =
        (R_XSPI0->WRAPCFG & ~R_XSPI0_WRAPCFG_DSSFTCS1_Msk) |
        ((wrap << R_XSPI0_WRAPCFG_DSSFTCS1_Pos) & R_XSPI0_WRAPCFG_DSSFTCS1_Msk);

    int ddrsmpex = 4;
    R_XSPI0->LIOCFGCS[1] =
        (R_XSPI0->LIOCFGCS[1] & ~R_XSPI0_LIOCFGCS_DDRSMPEX_Msk) |
        ((ddrsmpex << R_XSPI0_LIOCFGCS_DDRSMPEX_Pos) & R_XSPI0_LIOCFGCS_DDRSMPEX_Msk);
    __DMB();

    // write enable
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE_ENABLE, 2,
                         0x00000000, 0,
                         0, 0,
                         0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }

    // write CR0
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE_REGISTER, OSPI_B_COMMAND_BYTES_2,
                         0x00000004, 4,
                         0x2D8F, 2, // 64Byte burst, Latency 7
                         0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }

    // write enable
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE_ENABLE, 2,
                         0x00000000, 0,
                         0, 0,
                         0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }

    // write CR1
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE_REGISTER, 2,
                         0x00000006, 4,
                         0xC1FF, 2, // CK+,CK-
                         0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }

    // read CR0
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ_REGISTER, 2,
                         0x00000004, 4,
                         0x00, 2,
                         15, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }
    xprintf("CR0=0x%04x\n", g_ospi0_trans.data);

    // read CR1
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ_REGISTER, 2,
                         0x00000006, 4,
                         0x00, 2,
                         15, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }
    xprintf("CR1=0x%04x\n", g_ospi0_trans.data);

    // 正常終了
    xprintf("[OSPI] RW end\n");

    return err;
}
