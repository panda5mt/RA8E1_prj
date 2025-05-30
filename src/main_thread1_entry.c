#include "main_thread1.h"
#include "putchar_ra8usb.h"

#include "main_thread1.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"

#define UDP_PORT_DEST 9000

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

    dhcp_start(&netif);

    // DHCP待機タイマ（タイムアウト = 10秒）
    for (int i = 0; i < 100; i++)
    {
        if (netif.ip_addr.addr != 0)
        {
            xprintf("[LwIP] DHCP assigned IP: %s\n", ip4addr_ntoa(&netif.ip_addr));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
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
    const char *message = "Hello from RA8E1 UDP";
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
