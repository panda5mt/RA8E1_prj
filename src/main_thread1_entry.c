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
#include "r_spi_flash_api.h"
#include "ra/fsp/src/bsp/mcu/all/bsp_io.h"

#include <string.h> // for memcpy

#define UDP_PORT_DEST 9000

#define HYPERRAM_BASE_ADDR ((void *)0x90000000U) /* Device on CS1 */
#define TEST_DATA_LENGTH (64U * 256U)            // テストデータ長

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

bool cb_flag = false;

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

const uint8_t g_preamble_bytes[OSPI_B_DOPI_PREAMBLE_PATTERN_LENGTH_BYTES] =
    {
        0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x08, 0x00,
        0x00, 0xF7, 0xFF, 0x00, 0x08, 0xF7, 0x00, 0xF7};

/* Custom command sets. */
ospi_b_xspi_command_set_t g_command_sets[] =
    {
        /* 8D-8D-8D example with inverted lower command byte. */
        [0] = {
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
fsp_err_t ospi_memcpy(uint8_t *p_dest, uint8_t *p_src, uint32_t length)
{
    uint32_t burst_size = 64; // in bytes
    spi_flash_direct_transfer_t g_ospi0_trans;
    fsp_err_t err = FSP_SUCCESS;
    while (length >= 0)
    {

        if (length <= burst_size) // final write
        {
            err = R_OSPI_B_Write(
                &g_ospi0_ctrl,
                p_src,
                p_dest,
                length);
            if (FSP_SUCCESS != err)
            {
                xprintf("[OSPI] write error!\n");
                break;
            }

            while (cb_flag == false) // wait for DMAC transfer done
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            // xprintf("[OSPI] CB!!!!!!!!\n");
            cb_flag = false;
            break;
        }
        else // burst write
        {
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
            err = R_OSPI_B_Write(&g_ospi0_ctrl, p_src, p_dest, burst_size);
            if (FSP_SUCCESS != err)
            {
                xprintf("[OSPI] write error!\n");
                break;
            }
            while (cb_flag == false) // wait for DMAC transfer done
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            // xprintf("[OSPI] CB_!!!!!!!!\n");
            cb_flag = false;
            length -= burst_size;
            p_dest += burst_size;
            p_src += burst_size;
        }
    }

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

    // write CR0
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE_REGISTER, 2,
                         0x00000004, 4,
                         0x2f8F, 2,
                         0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
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
        return;
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
        return;
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
        return;
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
        return;
    }
    xprintf("CR1=0x%04x\n", g_ospi0_trans.data);

    // 2. 書き込みデータ作成
    uint8_t write_data[TEST_DATA_LENGTH];
    uint8_t read_data[TEST_DATA_LENGTH];

    int za = 16;
    R_XSPI0->WRAPCFG =
        (R_XSPI0->WRAPCFG & ~R_XSPI0_WRAPCFG_DSSFTCS1_Msk) |
        ((za << R_XSPI0_WRAPCFG_DSSFTCS1_Pos) & R_XSPI0_WRAPCFG_DSSFTCS1_Msk);
    __DMB();

    // 書き込みデータ代入
    for (uint32_t i = 0; i < TEST_DATA_LENGTH; i++)
    {
        write_data[i] = (addr_prng_byte(i, 0x12345678u) & 0xFF);
        read_data[i] = 0x00;
    }

    // 3. 書き込み先アドレス（HyperRAM内）
    uint8_t *hyperram_ptr = (uint8_t *)HYPERRAM_BASE_ADDR;

    // 4. 書き込み（メモリマップドアクセス)
    SCB_CleanDCache();   // 念のため
    SCB_DisableDCache(); // 一時的にOFF

    // 書き込みはR_OSPI_B_Write
    err = R_OSPI_B_Write(&g_ospi0_ctrl, &write_data[0], &hyperram_ptr[0], TEST_DATA_LENGTH);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }

    int z = 1;
    int rerror = 0;
    R_XSPI0->WRAPCFG =
        (R_XSPI0->WRAPCFG & ~R_XSPI0_WRAPCFG_DSSFTCS1_Msk) |
        ((z << R_XSPI0_WRAPCFG_DSSFTCS1_Pos) & R_XSPI0_WRAPCFG_DSSFTCS1_Msk);
    __DMB();

    SCB_CleanDCache();   // 念のため
    SCB_DisableDCache(); // 一時的にOFF

    vTaskDelay(pdMS_TO_TICKS(1000));

    // 5. 読み出しバッファ(プリフェッチがないとデータが取れないみたい)
    memcpy(read_data, hyperram_ptr, TEST_DATA_LENGTH * sizeof(char));

    // 6. 検証
    for (uint32_t i = 0; i < TEST_DATA_LENGTH; i++)
    {
        if (read_data[i] != write_data[i])
        {
            xprintf("[OSPI] data error at %d: 0x%02x!=0x%02x\n", i, read_data[i], write_data[i]);
            rerror++;
        }
    }
    xprintf("[OSPI] margin z=%d,error = %d\n", z, rerror);

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

    // read CR1
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ_REGISTER, 2,
                         0x00000006, 4,
                         0x00, 2,
                         15, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return;
    }
    xprintf("CR1=0x%04x\n", g_ospi0_trans.data);

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