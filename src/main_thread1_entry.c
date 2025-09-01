#include "main_thread1.h"
#include "putchar_ra8usb.h"
#include "hal_data.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"

#include "r_ospi_b.h"
#include "r_spi_flash_api.h"
#include "ra/fsp/src/bsp/mcu/all/bsp_io.h"

#include <string.h> // for memcpy

#define UDP_PORT_DEST 9000

#define HYPERRAM_BASE_ADDR ((void *)0x90000000U) /* Device on CS1 */
#define TEST_DATA_LENGTH (64U)                   // テストデータ長

// COMMAND SET(infineon S80KS5123)
// #define  <COMMAND>               <CODE>     <CA-DATA> | <ADDRESS(bytes)>   | <Latency cycles>  | <Data (bytes)>
#define OSPI_B_COMMAND_RESET_ENABLE (0x6666)   // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_RESET (0x9999)          // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_READ_ID (0x9F9F)        // 8-8-8  |  0x00(4bytes)      |  3-7              |  (4bytes)
#define OSPI_B_COMMAND_POWER_DOWN (0xB9B9)     // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_READ (0xEEEE)           // 8-8-8  |  (4bytes)          |  3-7              |  1 to \infty
#define OSPI_B_COMMAND_WRITE (0xDEDE)          // 8-8-8  |  (4bytes)          |  3-7              |  1 to \infty
#define OSPI_B_COMMAND_WRITE_ENABLE (0x0606)   // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_WRITE_DISABLE (0x0404)  // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_READ_REGISTER (0x6565)  // 8-8-8  |  (4bytes)          |  3-7              |  (2bytes)
#define OSPI_B_COMMAND_WRITE_REGISTER (0x7171) // 8-8-8  |  (4bytes)          |  0                |  (2bytes)

#define OSPI_B_DOPI_PREAMBLE_PATTERN_LENGTH_BYTES (16U)
#define OSPI_B_EXAMPLE_PREAMBLE_ADDRESS (HYPERRAM_BASE_ADDR) /* Device connected to CS1 */

const uint8_t g_preamble_bytes[OSPI_B_DOPI_PREAMBLE_PATTERN_LENGTH_BYTES] =
    {
        0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x08, 0x00,
        0x00, 0xF7, 0xFF, 0x00, 0x08, 0xF7, 0x00, 0xF7};

/* Custom command sets. */
ospi_b_xspi_command_set_t g_command_sets[] =
    {
        /* 8D-8D-8D example with inverted lower command byte. */
        {
            .protocol = SPI_FLASH_PROTOCOL_8D_8D_8D,
            .latency_mode = OSPI_B_LATENCY_MODE_FIXED,
            .frame_format = OSPI_B_FRAME_FORMAT_XSPI_PROFILE_2,
            .command_bytes = OSPI_B_COMMAND_BYTES_2,
            .address_bytes = 4U,
            .read_command = OSPI_B_COMMAND_READ,
            .program_command = OSPI_B_COMMAND_WRITE,
            .write_enable_command = OSPI_B_COMMAND_WRITE_ENABLE,
            .status_command = NULL,
            .address_msb_mask = 0x01,
            .read_dummy_cycles = 16U,
            .program_dummy_cycles = 16U,

            .status_dummy_cycles = NULL,
            .p_erase_commands = NULL}};

fsp_err_t ospi_raw_trans(spi_flash_direct_transfer_t *p_trans,
                         uint32_t command, uint8_t cmd_len,
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

void ospi_hyperram_test(void)
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
        return;
    }
    xprintf("[OSPI] init Ok\n");

    spi_flash_direct_transfer_t g_ospi0_trans;

    // write enable
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE_ENABLE, 2,
                         0x00000000, 0,
                         0, 0,
                         0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }

    // write memory
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE, 2,
                         0x00000080, 4,
                         0xDEADBEEF, 4,
                         15, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }

    // read ID0/ID1
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ_ID, 2,
                         0x00000000, 4,
                         0, 4,
                         15, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }
    xprintf("ID0/ID1=0x%08x\n", g_ospi0_trans.data);

    // read CR0
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ_REGISTER, 2,
                         0x00000004, 4,
                         0x00, 2,
                         15, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }
    xprintf("CR0=0x%04x\n", g_ospi0_trans.data);

    // read memory
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ, 2,
                         0x00000080, 4,
                         0x00, 4,
                         15, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }
    xprintf("Data=0x%08x\n", g_ospi0_trans.data);

    // write enable
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE_ENABLE, 2,
                         0x00000000, 0,
                         0, 0,
                         0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }

    // 2. 書き込みデータ作成
    uint8_t write_data[TEST_DATA_LENGTH];
    uint8_t read_data[TEST_DATA_LENGTH];

    for (uint32_t i = 0; i < TEST_DATA_LENGTH; i++)
    {
        write_data[i] = 255 - (uint8_t)(i & 0xff);
        read_data[i] = 0x00;
    }

    // 3. 書き込み先アドレス（HyperRAM内）
    uint8_t *hyperram_ptr = (uint8_t *)HYPERRAM_BASE_ADDR;

    // 4. 書き込み（メモリマップドアクセス）
    // memcpy(&hyperram_ptr[0], &write_data[0], TEST_DATA_LENGTH);
    R_OSPI_B_Write(&g_ospi0_ctrl,
                   (uint8_t const *const)&write_data[0],
                   (uint8_t *const)&hyperram_ptr[0],
                   TEST_DATA_LENGTH);

    vTaskDelay(pdMS_TO_TICKS(100));

    // 5. 読み出しバッファ
    // memcpy(&read_data[0], &hyperram_ptr[0], TEST_DATA_LENGTH);
    R_OSPI_B_Write(&g_ospi0_ctrl,
                   (uint8_t const *const)&hyperram_ptr[0],
                   (uint8_t *const)&read_data[0],
                   TEST_DATA_LENGTH);

    // 6. 検証
    for (uint32_t i = 0; i < TEST_DATA_LENGTH; i++)
    {
        if (read_data[i] != write_data[i])
        {
            xprintf("[OSPI] data error at %d: 0x%02x\n", i, read_data[i]);
        }
    }
    // 正常終了
    xprintf("[OSPI] RW end\n");
}

void main_thread1_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    // LAN8720A Reset
    R_BSP_PinAccessEnable();
    R_BSP_PinWrite(LAN8720_nRST, BSP_IO_LEVEL_LOW);
    vTaskDelay(pdMS_TO_TICKS(300));
    R_BSP_PinWrite(LAN8720_nRST, BSP_IO_LEVEL_HIGH);
    vTaskDelay(pdMS_TO_TICKS(300));

    xprintf("[ETH] LAN8720A Ready\n");
    ospi_hyperram_test();
    struct netif netif;

    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gw;

    IP_ADDR4(&ipaddr, 0, 0, 0, 0);  // IPADDR_ANY
    IP_ADDR4(&netmask, 0, 0, 0, 0); // IPADDR_ANY
    IP_ADDR4(&gw, 0, 0, 0, 0);      // IPADDR_ANY

    lwip_init();

    netif_add(&netif, &ipaddr, &netmask, &gw, &g_lwip_ether0_instance, rm_lwip_ether_init, netif_input);
    netif_set_default(&netif);
    netif_set_up(&netif);
    netif_set_link_up(&netif);

    dhcp_start(&netif);

    // DHCP待機
    for (int i = 0; i < 2000; i++)
    {
        sys_check_timeouts();
        /*        if (netif.ip_addr.addr != 0)
                {
                    xprintf("[LwIP] DHCP assigned IP: %s\n", ip4addr_ntoa(&netif.ip_addr));
                    break;
                }
        */
        if (!ip4_addr_isany_val(*netif_ip4_addr(&netif)))
        {
            xprintf("[LwIP] DHCP assigned IP: %s\n", ip4addr_ntoa(netif_ip4_addr(&netif)));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // if DHCP is not valid, AUTOIP will Start
    if (ip4_addr_isany_val(*netif_ip4_addr(&netif)))
    {
        xprintf("[LwIP] DHCP failed. Using AutoIP.\n");
#if LWIP_AUTOIP
        autoip_start(&netif);
        for (int i = 0; i < 150; i++)
        {
            sys_check_timeouts();
            if (!ip4_addr_isany_val(*netif_ip4_addr(&netif)))
                break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        xprintf("[LwIP] AutoIP assigned IP: %s\n", ip4addr_ntoa(netif_ip4_addr(&netif)));
#else
        xprintf("[LwIP] AUTOIP disabled\n");
#endif
    }

    // UDP通信準備
    const char *message = "Hello from RA8E1 UDP!! Hello World!!";
    struct udp_pcb *pcb = udp_new();
    if (!pcb)
    {
        xprintf("[UDP] udp_new failed\n");
        return;
    }

    // ip_addr_t broadcast_ip;
    // broadcast_ip.addr = (netif.ip_addr.addr & netif.netmask.addr) | ~netif.netmask.addr;

    ip4_addr_t broadcast_ip;
    u32_t ip = ip4_addr_get_u32(netif_ip4_addr(&netif));
    u32_t mask = ip4_addr_get_u32(netif_ip4_netmask(&netif));
    ip4_addr_set_u32(&broadcast_ip, (ip & mask) | ~mask);

    for (int i = 0; i < 100; i++)
    {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, strlen(message), PBUF_RAM);
        if (!p)
        {
            xprintf("[UDP] pbuf_alloc failed\n");
            break;
        }

        memcpy(p->payload, message, strlen(message));

        err_t err = udp_sendto(pcb, p, &broadcast_ip, UDP_PORT_DEST);
        if (err == ERR_OK)
        {
            xprintf("[UDP] #%d sent OK\n", i + 1);
        }
        else
        {
            xprintf("[UDP] #%d send failed: %d\n", i + 1, err);
        }

        pbuf_free(p);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    udp_remove(pcb);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
