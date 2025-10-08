#include "main_thread1.h"
#include "putchar_ra8usb.h"
#include "hal_data.h"

#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/dhcp.h"
#if LWIP_AUTOIP
#include "lwip/autoip.h"
#endif

#include "ra/fsp/src/bsp/mcu/all/bsp_io.h"

#define UDP_PORT_DEST 9000

static void netif_status_cb(struct netif *n);
static void udp_rx_cb(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port);
typedef struct
{
    struct udp_pcb *pcb;
    ip_addr_t dest_ip;
    const char *msg;
    uint16_t port;
    int remaining;
    uint32_t interval_ms;
} udp_send_ctx_t;
static void udp_send_timer_cb(void *arg);

/* DHCP完了待ち用セマフォ */
static SemaphoreHandle_t g_ip_ready_sem = NULL;

/* ====== 受信コールバック ====== */
static void udp_rx_cb(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port)
{
    FSP_PARAMETER_NOT_USED(arg);
    FSP_PARAMETER_NOT_USED(upcb);
    if (!p)
    {
        return;
    }

    char head[65] = {0};
    u16_t cpy = (p->tot_len < 64) ? p->tot_len : 64;
    /* p->payload は線形とは限らないが、ここでは小さく読むだけなので p->payload を直接 */
    memcpy(head, p->payload, cpy);

    xprintf("[UDP RX] %s:%u len=%u data=\"%s\"\n",
            ip4addr_ntoa(ip_2_ip4(addr)), port, p->tot_len, head);

    pbuf_free(p);
}

/* ====== 送信タイマ（tcpip_thread 上で実行） ====== */
static void udp_send_timer_cb(void *arg)
{
    udp_send_ctx_t *ctx = (udp_send_ctx_t *)arg;
    if (!ctx || !ctx->pcb)
        return;

    const size_t len = strlen(ctx->msg);
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (p)
    {
        memcpy(p->payload, ctx->msg, len);
        err_t e = udp_sendto(ctx->pcb, p, &ctx->dest_ip, ctx->port);
        pbuf_free(p);
        xprintf("[UDP] send %s, remain=%d\n", (e == ERR_OK) ? "OK" : "NG", ctx->remaining - 1);
    }
    else
    {
        xprintf("[UDP] pbuf_alloc failed\n");
    }

    if (--ctx->remaining > 0)
    {
        /* 次回も tcpip_thread のタイマで */
        sys_timeout(ctx->interval_ms, udp_send_timer_cb, ctx);
    }
    else
    {
        udp_remove(ctx->pcb);
        ctx->pcb = NULL;
        xprintf("[UDP] done\n");
        vPortFree(ctx);
    }
}

/* ====== netif ステータスコールバック（tcpip_thread から呼ばれる） ====== */
static void netif_status_cb(struct netif *n)
{
    if (!ip4_addr_isany_val(*netif_ip4_addr(n)))
    {
        if (g_ip_ready_sem)
        {
            xSemaphoreGive(g_ip_ready_sem);
        }
    }
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

    /* netif 準備 */
    struct netif netif;
    ip4_addr_t ipaddr, netmask, gw;
    ip4_addr_set_zero(&ipaddr);
    ip4_addr_set_zero(&netmask);
    ip4_addr_set_zero(&gw);

    IP_ADDR4(&ipaddr, 0, 0, 0, 0);  // IPADDR_ANY
    IP_ADDR4(&netmask, 0, 0, 0, 0); // IPADDR_ANY
    IP_ADDR4(&gw, 0, 0, 0, 0);      // IPADDR_ANY

    /* LwIP: tcpip_thread を起動（以後のタイマ/DHCP/ARP はこのスレッドが管理） */
    tcpip_init(NULL, NULL);

    vTaskDelay(pdMS_TO_TICKS(100)); // ← tcpip_thread 起動を待つ

    /* rm_lwip_ether 初期化（input は tcpip_input を指定） */
    netif_add(&netif, &ipaddr, &netmask, &gw,
              &g_lwip_ether0_instance, /* ← プロジェクト固有。名前が違う場合は置換 */
              rm_lwip_ether_init,
              tcpip_input);
    netif_set_default(&netif);

    /* IP 取得通知のためのステータスコールバック登録 */
    g_ip_ready_sem = xSemaphoreCreateBinary();
    netif_set_status_callback(&netif, netif_status_cb);

    /* Link/IF Up は netifapi_* でスレッド安全に */
    netifapi_netif_set_up(&netif);
    netifapi_netif_set_link_up(&netif);

    /* DHCP 開始（tcpip_thread に依頼） */
    netifapi_dhcp_start(&netif);

    /* DHCP 完了待ち（sys_check_timeouts は不要） */
    if (xSemaphoreTake(g_ip_ready_sem, pdMS_TO_TICKS(20000)) == pdTRUE)
    {
        xprintf("[LwIP] DHCP IP: %s\n", ip4addr_ntoa(netif_ip4_addr(&netif)));
    }
    else
    {
#if LWIP_AUTOIP
        xprintf("[LwIP] DHCP timeout: AutoIP start...\n");
        netifapi_autoip_start(&netif);
        (void)xSemaphoreTake(g_ip_ready_sem, pdMS_TO_TICKS(10000));
        if (!ip4_addr_isany_val(*netif_ip4_addr(&netif)))
        {
            xprintf("[LwIP] AutoIP IP: %s\n", ip4addr_ntoa(netif_ip4_addr(&netif)));
        }
        else
        {
            xprintf("[LwIP] AutoIP timeout\n");
        }
#else
        xprintf("[LwIP] DHCP timeout (AUTOIP disabled)\n");
#endif
    }

    /* ====== UDP：受信コールバック＋送信を sys_timeout で ====== */
    if (!ip4_addr_isany_val(*netif_ip4_addr(&netif)))
    {

        /* サブネットブロードキャストを計算 */
        ip4_addr_t bcast4;
        {
            u32_t ip = ip4_addr_get_u32(netif_ip4_addr(&netif));
            u32_t mask = ip4_addr_get_u32(netif_ip4_netmask(&netif));
            ip4_addr_set_u32(&bcast4, (ip & mask) | ~mask);
        }
        ip_addr_t dest_ip;
        ip_addr_copy_from_ip4(dest_ip, bcast4);

        /* PCB 生成・受信ポートに bind（受信も見たい場合） */
        struct udp_pcb *pcb = udp_new();
        if (!pcb)
        {
            xprintf("[UDP] udp_new failed\n");
            goto forever;
        }

        if (udp_bind(pcb, IP_ADDR_ANY, UDP_PORT_DEST) != ERR_OK)
        {
            xprintf("[UDP] bind failed\n");
            udp_remove(pcb);
            goto forever;
        }

        /* 受信コールバック登録 */
        udp_recv(pcb, udp_rx_cb, NULL);

        /* 送信用コンテキストを確保し、tcpip_thread のタイマで駆動 */
        static const char *message = "Hello from RA8E1 UDP!! Hello World!!";
        udp_send_ctx_t *ctx = (udp_send_ctx_t *)pvPortMalloc(sizeof(*ctx));
        if (!ctx)
        {
            xprintf("[UDP] OOM\n");
            udp_remove(pcb);
            goto forever;
        }
        memset(ctx, 0, sizeof(*ctx));
        ctx->pcb = pcb;
        ctx->dest_ip = dest_ip;
        ctx->msg = message;
        ctx->port = UDP_PORT_DEST;
        ctx->remaining = 100;  /* 100回 */
        ctx->interval_ms = 20; /* 20ms 間隔 */

        /* 1発目をスケジュール（以降は udp_send_timer_cb 内で再スケジュール） */
        sys_timeout(1, udp_send_timer_cb, ctx);
    }

forever:
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
