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

#include "r_ospi_b.h"
#include "ra/fsp/src/bsp/mcu/all/bsp_io.h"

#include <string.h> // for memcpy
#include "hyperram_integ.h"

#define UDP_PORT_DEST 9000
#define TEST_DATA_LENGTH (64U * 1U)

bool cb_flag = false;

#ifndef DCACHE_LINE_SIZE
#define DCACHE_LINE_SIZE 32u
#endif

static inline void dcache_clean_range(const void *addr, size_t len)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start = (uintptr_t)addr & ~(DCACHE_LINE_SIZE - 1u);
    uintptr_t end = ((uintptr_t)addr + len + (DCACHE_LINE_SIZE - 1u)) & ~(DCACHE_LINE_SIZE - 1u);
    SCB_CleanDCache_by_Addr((void *)start, (int32_t)(end - start));
#else
    (void)addr;
    (void)len;
#endif
}

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

// ★ OSPI レジスタの要点ダンプ（ch=0/1 の両方見ます）
static void ospi_dump_regs(void)
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
static inline void ospi_wait_mmap_idle(void)
{
    const uint32_t BUSY_MASK = (0x03u << R_XSPI0_COMSTT_MEMACCCH_Pos);
    while (R_XSPI0->COMSTT & BUSY_MASK)
    {
        __NOP();
    }
}

uint8_t write_data[TEST_DATA_LENGTH];
uint8_t read_data[TEST_DATA_LENGTH];
// アドレス→擬似乱数(1byte)。seed を変えるとパターンが変わる（再現性あり）
static inline uint8_t addr_prng_byte(uint32_t addr, uint32_t seed)
{
    uint32_t x = addr ^ seed;
    x += 0x9E3779B9u; // golden ratio
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return (uint8_t)x;
}

void ospi_hyperram_test(void)
{
    fsp_err_t err = FSP_SUCCESS;

    err = hyperram_init();
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] HyperRAM init error!\n");
        return;
    }

    // // write enable
    // err = ospi_raw_trans(&g_ospi0_trans,
    //                      OSPI_B_COMMAND_WRITE_ENABLE, 2,
    //                      0x00000000, 0,
    //                      0, 0,
    //                      0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    // if (FSP_SUCCESS != err)
    // {
    //     xprintf("[OSPI] direct transfer error!\n");
    // }

    // 3. 書き込み先アドレス（HyperRAM内）
    uint8_t *hyperram_ptr = (uint8_t *)HYPERRAM_BASE_ADDR;

    ospi_dump_regs();

    cb_flag = true;
    for (int jj = 0; jj < /*(int)(64 * 1024 * 1024 / TEST_DATA_LENGTH)*/ 1; jj++)
    {
        // xprintf("hyperram_ptr=0x%X\n", hyperram_ptr); // debug

        // 書き込みデータ代入
        for (uint32_t i = 0; i < TEST_DATA_LENGTH; i++)
        {
            write_data[i] = (addr_prng_byte(i + jj * TEST_DATA_LENGTH, 0x12345678u) & 0xFF);
        }
        dcache_clean_range(write_data, TEST_DATA_LENGTH); //
        // 4. 書き込み（R_OSPI_B_Write)
        // write
        // for (uint32_t z = 0; z < TEST_DATA_LENGTH; z += 4)
        // {
        //     uint32_t data = (write_data[z] << 0) | (write_data[z + 1] << 8) | (write_data[z + 2] << 16) | (write_data[z + 3] << 24);
        //     uint32_t adr = z + jj * TEST_DATA_LENGTH;
        //     err = ospi_raw_trans(&g_ospi0_trans,
        //                          OSPI_B_COMMAND_WRITE, 2,
        //                          adr, 4,
        //                          data, 4,
        //                          15, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
        //     if (FSP_SUCCESS != err)
        //     {
        //         xprintf("[OSPI] direct transfer error!\n");
        //     }
        // }

        err = R_OSPI_B_Write(&g_ospi0_ctrl, &write_data[0], &hyperram_ptr[0], TEST_DATA_LENGTH);
        if (FSP_SUCCESS != err)
        {
            xprintf("[OSPI] direct transfer error!\n");
            return;
        }
        // ospi_wait_mmap_idle();

        // ★押し出し＆完了待ち（毎回）
        __DMB();
        R_XSPI0->BMCTL1 = (0x03u << R_XSPI0_BMCTL1_MWRPUSHCH_Pos); // MWRPUSH
        const uint32_t BUSY_MASK = (0x03u << R_XSPI0_COMSTT_MEMACCCH_Pos);
        while (R_XSPI0->COMSTT & BUSY_MASK)
        {
            __NOP();
        }

        hyperram_ptr += TEST_DATA_LENGTH; // 次のバッファへ
    }

    // xprintf("hyperram_read\n"); // debug
    // hyperram_ptr = HYPERRAM_BASE_ADDR;

    // for (int jj = 0; jj < /*(int)(64 * 1024 * 1024 / TEST_DATA_LENGTH)*/ 1; jj++)
    // {
    //     // xprintf("hyperram_ptr=0x%X\n", hyperram_ptr); // debug

    //     // 書き込みデータ代入
    //     for (uint32_t i = 0; i < TEST_DATA_LENGTH; i++)
    //     {
    //         write_data[i] = (addr_prng_byte(i + jj * TEST_DATA_LENGTH, 0x12345678u) & 0xFF);
    //     }

    //     // 5. 読み出しバッファ(プリフェッチがないとデータが取れないみたい)
    //     if (memcmp(write_data, hyperram_ptr, TEST_DATA_LENGTH * sizeof(char)) != 0)
    //     {
    //         xprintf("[OSPI] prefetch error!\n");
    //     }

    //     int rerror = 0;
    //     // 6. 検証
    //     for (uint32_t i = 0; i < TEST_DATA_LENGTH; i++)
    //     {
    //         if (write_data[i] != hyperram_ptr[i])
    //         {
    //             xprintf("[OSPI] data error at %d: 0x%02x!=0x%02x\n", i, write_data[i], hyperram_ptr[i]);
    //             rerror++;
    //         }
    //     }

    //     xprintf("[OSPI] error = %d\n", rerror);
    //     hyperram_ptr += TEST_DATA_LENGTH; // 次のバッファへ
    // }

    for (int jj = 0; jj < 1; jj++) // (int)(64 * 1024 * 1024 / TEST_DATA_LENGTH)
    {
        // xprintf("hyperram_ptr=0x%X\n", hyperram_ptr); // debug

        for (uint32_t z = 0; z < TEST_DATA_LENGTH; z += 4)
        {
            uint32_t adr = z + jj * TEST_DATA_LENGTH;
            err = ospi_raw_trans(&g_ospi0_trans,
                                 OSPI_B_COMMAND_READ, 2,
                                 adr, 4,
                                 0, 4,
                                 15, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
            if (FSP_SUCCESS != err)
            {
                xprintf("[OSPI] direct transfer error!\n");
            }

            read_data[z + 0] = (g_ospi0_trans.data) & 0xFF;
            read_data[z + 1] = (g_ospi0_trans.data >> 8) & 0xFF;
            read_data[z + 2] = (g_ospi0_trans.data >> 16) & 0xFF;
            read_data[z + 3] = (g_ospi0_trans.data >> 24) & 0xFF;
        }
        hyperram_ptr += TEST_DATA_LENGTH; // 次のバッファへ
    }

    if (memcmp(write_data, read_data, TEST_DATA_LENGTH * sizeof(char)) != 0)
    {
        // xprintf("[OSPI] prefetch error!\n");
    }

    int rerror = 0;
    for (int ii = 0; ii < TEST_DATA_LENGTH; ii++)
    {
        if (write_data[ii] != read_data[ii])
        {
            // xprintf("[OSPI] data error at %d: 0x%02x!=0x%02x\n", ii, write_data[ii], read_data[ii]);
            rerror++;
        }
    }

    // 正常終了
    xprintf("[OSPI] RW end, error=%d\n", rerror);
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

void ospi_dmac_cb(transfer_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    cb_flag = true;
    // xprintf("OSPI DMAC transfer done.\n");
}