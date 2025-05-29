#include "main_thread1.h"
#include "putchar_ra8usb.h"

#include "main_thread1.h"
#include "r_ether_phy_target_lan8720a.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "lwip/dhcp.h"

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

    // ネットワーク初期化
    netif_add(&netif, &ipaddr, &netmask, &gw, &g_lwip_ether0_instance, rm_lwip_ether_init, netif_input);
    netif_set_default(&netif);
    netif_set_up(&netif);
    netif_set_link_up(&netif);

    // DHCP Start
    dhcp_start(&netif);
    xprintf("[DHCP] Waiting for IP...\n");

    // wait until getting IP address
    while (netif.ip_addr.addr == 0)
    {
        sys_check_timeouts();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    xprintf("[DHCP] IP acquired: %s\n", ipaddr_ntoa(&netif.ip_addr));

    // UDP Payload
    const char *message = "Hello from Renesas RA8E1 UDP!! YEAHHHHHHHHHHHHHHHHH!!";
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, strlen(message), PBUF_RAM);
    if (!p)
    {
        xprintf("[UDP] pbuf_alloc failed\n");
        return;
    }
    memcpy(p->payload, message, strlen(message));

    // Make UDP PCB
    struct udp_pcb *pcb = udp_new();
    if (!pcb)
    {
        xprintf("[UDP] udp_new failed\n");
        pbuf_free(p);
        return;
    }

    // Destination Settings
    ip_addr_t dest_ip;
    IP4_ADDR(&dest_ip, 192, 168, 10, 123);
    for (int i = 0; i < 100; i++)
    {
        err_t err = udp_sendto(pcb, p, &dest_ip, UDP_PORT_DEST);
        if (err == ERR_OK)
        {
            xprintf("[UDP] Message sent\n");
        }
        else
        {
            xprintf("[UDP] Send failed: %d\n", err);
        }
    }

    pbuf_free(p);
    udp_remove(pcb);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
