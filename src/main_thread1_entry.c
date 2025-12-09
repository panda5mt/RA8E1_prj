#include "main_thread1.h"
#include "putchar_ra8usb.h"
#include "hal_data.h"
#include "hyperram_integ.h"

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

// UDP写真データチャンクヘッダー
typedef struct __attribute__((packed))
{
    uint32_t magic_number;    // マジックナンバー (0x12345678)
    uint32_t total_size;      // 写真データの総サイズ
    uint32_t chunk_index;     // 現在のチャンクインデックス (0から開始)
    uint32_t total_chunks;    // 総チャンク数
    uint32_t chunk_offset;    // このチャンクのオフセット（バイト）
    uint16_t chunk_data_size; // このチャンクのデータサイズ
    uint16_t checksum;        // ヘッダーのチェックサム
} udp_photo_header_t;

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
    // 写真データ送信用の追加フィールド
    uint8_t *photo_data; // 写真データのポインタ
    uint32_t photo_size; // 写真データの総サイズ
    uint32_t sent_bytes; // 送信済みバイト数
    uint32_t chunk_size; // 1回の送信サイズ（512バイト）
    bool is_photo_mode;  // 写真モードかどうか

    // マルチフレーム動画送信用
    bool is_video_mode;
    uint32_t current_frame;
    uint32_t total_frames;
    uint32_t frame_interval_ms; // フレーム間の待機時間
    bool is_frame_complete;
} udp_send_ctx_t;
static void udp_send_timer_cb(void *arg);

// ヘッダーチェックサム計算
static uint16_t calc_header_checksum(udp_photo_header_t *header)
{
    uint16_t *data = (uint16_t *)header;
    uint32_t sum = 0;
    size_t len = (sizeof(udp_photo_header_t) - sizeof(uint16_t)) / sizeof(uint16_t); // checksumフィールド除く

    for (size_t i = 0; i < len; i++)
    {
        sum += data[i];
    }

    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

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

    struct pbuf *p;
    size_t send_size;

    if (ctx->is_video_mode || ctx->is_photo_mode)
    {
        // 動画・写真データモード：512バイトずつ送信
        uint32_t remaining_bytes = ctx->photo_size - ctx->sent_bytes;
        send_size = (remaining_bytes < ctx->chunk_size) ? remaining_bytes : ctx->chunk_size;

        // ヘッダー + データのサイズでバッファを確保
        size_t total_packet_size = sizeof(udp_photo_header_t) + send_size;
        p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)total_packet_size, PBUF_RAM);
        if (p)
        {
            // ヘッダーを作成
            udp_photo_header_t header;
            header.magic_number = 0x12345678;
            header.total_size = ctx->photo_size;
            header.chunk_index = ctx->sent_bytes / ctx->chunk_size;
            header.total_chunks = (ctx->photo_size + ctx->chunk_size - 1) / ctx->chunk_size;
            header.chunk_offset = ctx->sent_bytes;
            header.chunk_data_size = (uint16_t)send_size;
            header.checksum = calc_header_checksum(&header);

            // パケットにヘッダーをコピー
            memcpy(p->payload, &header, sizeof(udp_photo_header_t));

            // HyperRAMから64バイト単位で読み込み（制限対応）
            uint8_t *dest_ptr = (uint8_t *)p->payload + sizeof(udp_photo_header_t);
            uint32_t remaining_size = send_size;
            uint32_t current_offset = 0;

            while (remaining_size > 0)
            {
                uint32_t read_size = (remaining_size > 64) ? 64 : remaining_size;
                uint32_t base_addr = ctx->sent_bytes + current_offset;
                uint32_t converted_addr = ((base_addr & 0xfffffff0) << 6) | (base_addr & 0x0f);

                // 64バイト以下の単位でコピー
                memcpy(dest_ptr + current_offset,
                       (uint8_t *)HYPERRAM_BASE_ADDR + converted_addr, read_size);

                current_offset += read_size;
                remaining_size -= read_size;
            }
            err_t e = udp_sendto(ctx->pcb, p, &ctx->dest_ip, ctx->port);
            pbuf_free(p);

            if (e == ERR_OK)
            {
                ctx->sent_bytes += send_size;
                // ログ出力を大幅に削減（パフォーマンス向上）
                if ((ctx->sent_bytes / ctx->chunk_size) % 100 == 0)
                {
                    if (ctx->is_video_mode)
                    {
                        xprintf("[VIDEO] F%u: %u/%u\n", ctx->current_frame + 1, ctx->sent_bytes, ctx->photo_size);
                    }
                }
            }
            // ログ出力を最小限に抑制（高速化）
            // 動画モードではログをほぼ出力しない
        }
    }
    else
    {
        // 従来のテキストメッセージモード
        static char dynamic_msg[128];
        uint32_t timestamp = xTaskGetTickCount();
        snprintf(dynamic_msg, sizeof(dynamic_msg),
                 "RA8E1 UDP Message #%d at %u ms",
                 101 - ctx->remaining, (unsigned int)(timestamp * portTICK_PERIOD_MS));

        send_size = strlen(dynamic_msg);
        p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)send_size, PBUF_RAM);
        if (p)
        {
            memcpy(p->payload, dynamic_msg, send_size);
            err_t e = udp_sendto(ctx->pcb, p, &ctx->dest_ip, ctx->port);
            pbuf_free(p);
            xprintf("[UDP] send %s, remain=%d\n", (e == ERR_OK) ? "OK" : "NG", ctx->remaining - 1);
        }
    }

    if (!p)
    {
        xprintf("[UDP] pbuf_alloc failed\n");
    }

    // 継続条件の判定
    bool should_continue = false;
    uint32_t next_interval = ctx->interval_ms;

    if (ctx->is_video_mode)
    {
        if (ctx->sent_bytes < ctx->photo_size)
        {
            // 現在のフレーム内でパケット送信継続
            should_continue = true;
            ctx->is_frame_complete = false;
        }
        else
        {
            // 現在のフレーム完了
            ctx->current_frame++;
            ctx->is_frame_complete = true;

            if (ctx->current_frame < ctx->total_frames)
            {
                // 次のフレームがある：フレーム間インターバルで待機
                ctx->sent_bytes = 0; // 次フレーム用にリセット
                should_continue = true;
                next_interval = ctx->frame_interval_ms; // フレーム間は長めの間隔
                // ログ出力を削減（10フレームごと）
                if (ctx->current_frame % 10 == 0)
                {
                    xprintf("[VIDEO] F%u/%u done\n", ctx->current_frame, ctx->total_frames);
                }
            }
            else
            {
                // 全フレーム完了
                xprintf("[VIDEO] All %u frames transmitted\n", ctx->total_frames);
            }
        }
    }
    else if (ctx->is_photo_mode)
    {
        should_continue = (ctx->sent_bytes < ctx->photo_size);
    }
    else
    {
        should_continue = (--ctx->remaining > 0);
    }

    if (should_continue)
    {
        /* 次回も tcpip_thread のタイマで */
        sys_timeout(next_interval, udp_send_timer_cb, ctx);
    }
    else
    {
        if (ctx->is_video_mode)
        {
            xprintf("[VIDEO] transmission complete: %u frames\n", ctx->total_frames);
        }
        else if (ctx->is_photo_mode)
        {
            xprintf("[PHOTO] transmission complete: %u bytes\n", ctx->sent_bytes);
        }
        else
        {
            xprintf("[UDP] done\n");
        }
        udp_remove(ctx->pcb);
        ctx->pcb = NULL;
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
        ctx->port = UDP_PORT_DEST;
        ctx->interval_ms = 1; /* 0ms間隔（lwIPタスクに余裕を持たせたい場合は3ms） */

        // 動画データ送信モードの設定（シングルフレームパターンを継承）
        ctx->is_video_mode = true;
        ctx->is_photo_mode = false;
        ctx->photo_data = (uint8_t *)HYPERRAM_BASE_ADDR; // HyperRAMベースアドレス
        ctx->photo_size = 320 * 240 * 2;                 // 320x240x2 = 153600 bytes
        ctx->sent_bytes = 0;
        ctx->chunk_size = 512; // 512バイトずつ送信

        // マルチフレーム設定
        ctx->current_frame = 0;
        ctx->total_frames = 1000;   // 1000フレーム送信
        ctx->frame_interval_ms = 2; // フレーム間2ms待機（thread0と同期、高速化）
        ctx->is_frame_complete = false;

        xprintf("[VIDEO] Starting %d frame transmission: %d bytes/frame, %d chunks/frame\n",
                ctx->total_frames, ctx->photo_size, (ctx->photo_size + ctx->chunk_size - 1) / ctx->chunk_size);

        /* 1発目をスケジュール（ネットワーク安定化のため500ms待機） */
        sys_timeout(500, udp_send_timer_cb, ctx);
    }

forever:
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
