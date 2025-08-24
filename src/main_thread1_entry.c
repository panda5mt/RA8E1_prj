#include "main_thread1.h"
#include "putchar_ra8usb.h"
#include "hal_data.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"
#include "r_spi_flash_api.h"

#include <string.h> // for memcpy

#define UDP_PORT_DEST 9000

#define HYPERRAM_BASE_ADDR ((void *)0x90000000U) /* Device on CS1 */
#define TEST_DATA_LENGTH (1024U * 1U)            // テストデータ長

void ospi_hyperram_test(void)
{
    fsp_err_t err = FSP_SUCCESS;

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

    // reset enable
    g_ospi0_trans.command = 0x6666;
    g_ospi0_trans.command_length = 2;
    g_ospi0_trans.address = 0x00000000;
    g_ospi0_trans.address_length = 0;
    g_ospi0_trans.data_length = 0;
    g_ospi0_trans.dummy_cycles = 0;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &g_ospi0_trans, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }

    // reset
    g_ospi0_trans.command = 0x9999;
    g_ospi0_trans.command_length = 2;
    g_ospi0_trans.address = 0x00000000;
    g_ospi0_trans.address_length = 0;
    g_ospi0_trans.data_length = 0;
    g_ospi0_trans.dummy_cycles = 0;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &g_ospi0_trans, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }

    // read CR0
    g_ospi0_trans.command = 0x6565; // 0x9f9f;
    g_ospi0_trans.command_length = 2;
    g_ospi0_trans.address = 0x00000004;
    g_ospi0_trans.address_length = 4;
    g_ospi0_trans.data_length = 2;
    g_ospi0_trans.dummy_cycles = 15;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &g_ospi0_trans, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }
    xprintf("CR0=0x%04x\n", g_ospi0_trans.data);

    // read ID0
    g_ospi0_trans.command = 0x6565; // 0x9f9f;
    g_ospi0_trans.command_length = 2;
    g_ospi0_trans.address = 0x00000000;
    g_ospi0_trans.address_length = 4;
    g_ospi0_trans.data_length = 2;
    g_ospi0_trans.dummy_cycles = 15;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &g_ospi0_trans, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }
    xprintf("ID0=0x%04x\n", g_ospi0_trans.data);

    // read ID1
    g_ospi0_trans.command = 0x6565; // 0x9f9f;
    g_ospi0_trans.command_length = 2;
    g_ospi0_trans.address = 0x00000002;
    g_ospi0_trans.address_length = 4;
    g_ospi0_trans.data_length = 2;
    g_ospi0_trans.dummy_cycles = 15;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &g_ospi0_trans, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }
    xprintf("ID1=0x%04x\n", g_ospi0_trans.data);

    // write enable
    g_ospi0_trans.command = 0x0606;
    g_ospi0_trans.command_length = 2;
    g_ospi0_trans.address = 0x00000000;
    g_ospi0_trans.address_length = 0;
    g_ospi0_trans.data_length = 0;
    g_ospi0_trans.dummy_cycles = 0;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &g_ospi0_trans, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }

    // write memory
    g_ospi0_trans.command = 0xDEDE;
    g_ospi0_trans.command_length = 2;
    g_ospi0_trans.address = 0x00000080;
    g_ospi0_trans.address_length = 4;
    g_ospi0_trans.data_length = 4;
    g_ospi0_trans.data = 0x11223344;
    g_ospi0_trans.dummy_cycles = 15;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &g_ospi0_trans, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }
    xprintf("[OSPI] write Ok\n");

    // read ID0/ID1
    g_ospi0_trans.command = 0x9f9f;
    g_ospi0_trans.command_length = 2;
    g_ospi0_trans.address = 0x00000000;
    g_ospi0_trans.address_length = 4;
    g_ospi0_trans.data_length = 4;
    g_ospi0_trans.dummy_cycles = 15;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &g_ospi0_trans, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }
    xprintf("ID0/ID1=0x%08x\n", g_ospi0_trans.data);

    // read memory
    g_ospi0_trans.command = 0xEEEE;
    g_ospi0_trans.command_length = 2;
    g_ospi0_trans.address = 0x00000080;
    g_ospi0_trans.address_length = 4;
    g_ospi0_trans.data_length = 4;
    g_ospi0_trans.data = 0;

    g_ospi0_trans.dummy_cycles = 15;

    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &g_ospi0_trans, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }

    xprintf("[OSPI] data: %0x\n", g_ospi0_trans.data);

    while (1)
        ;

    // 2. 書き込みデータ作成
    uint8_t write_data[TEST_DATA_LENGTH];
    uint8_t read_data[TEST_DATA_LENGTH];

    for (uint32_t i = 0; i < TEST_DATA_LENGTH; i++)
    {
        write_data[i] = 255 - (uint8_t)(i & 0xff);
        // read_data[i] = 0x00;
    }

    // 3. 書き込み先アドレス（HyperRAM内）
    uint8_t *hyperram_ptr = (uint8_t *)HYPERRAM_BASE_ADDR;

    // 4. 書き込み（メモリマップドアクセス）
    memcpy(&hyperram_ptr[0], &write_data[0], TEST_DATA_LENGTH);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 5. 読み出しバッファ
    memcpy(&read_data[0], &hyperram_ptr[0], TEST_DATA_LENGTH);

    // 6. 検証（任意）
    for (uint32_t i = 0; i < TEST_DATA_LENGTH; i++)
    {
        if (read_data[i] != write_data[i])
        {
            xprintf("[OSPI] data error at %d: %d\n", i, read_data[i]);
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
        if (netif.ip_addr.addr != 0)
        {
            xprintf("[LwIP] DHCP assigned IP: %s\n", ip4addr_ntoa(&netif.ip_addr));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // while (netif.ip_addr.addr == 0)
    //     ;
    // xprintf("[LwIP] DHCP assigned IP1: %s\n", ip4addr_ntoa(&netif.ip_addr));

    // if DHCP is not valid, AUTOIP will Start
    if (netif.ip_addr.addr == 0)
    {
        xprintf("[LwIP] DHCP failed. Using AutoIP.\n");
        autoip_start(&netif);

        while (netif.ip_addr.addr == 0)
        {
            sys_check_timeouts();
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        xprintf("[LwIP] AutoIP assigned IP: %s\n", ip4addr_ntoa(&netif.ip_addr));
    }

    // UDP通信準備
    const char *message = "Hello from RA8E1 UDP!! Hello World!!";
    struct udp_pcb *pcb = udp_new();
    if (!pcb)
    {
        xprintf("[UDP] udp_new failed\n");
        return;
    }

    // ip_addr_t dest_ip;
    // IP4_ADDR(&dest_ip, 192, 168, 10, 123); // 送信先

    ip_addr_t broadcast_ip;
    broadcast_ip.addr = (netif.ip_addr.addr & netif.netmask.addr) | ~netif.netmask.addr;

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
