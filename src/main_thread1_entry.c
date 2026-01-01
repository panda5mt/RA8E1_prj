#include "main_thread1.h"
#include "putchar_ra8usb.h"
#include "hal_data.h"
#include "hyperram_integ.h"
#include "video_frame_buffer.h"

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

#include <stdint.h>

#include "ra/fsp/src/bsp/mcu/all/bsp_io.h"

typedef enum
{
    YUV422_ORDER_UNKNOWN = 0,
    YUV422_ORDER_YUYV = 1,        /* [Y0 U0 Y1 V0] */
    YUV422_ORDER_UYVY = 2,        /* [U0 Y0 V0 Y1] */
    YUV422_ORDER_VYUY = 3,        /* [V0 Y1 U0 Y0] */
    YUV422_ORDER_YUYV_SWAP_Y = 4, /* swap Y within each pair: output (Y1,Y0) */
    YUV422_ORDER_UYVY_SWAP_Y = 5  /* swap Y within each pair: output (Y1,Y0) */
} yuv422_order_t;

/*
 * YUV422バイト順の固定設定（MATLAB側は変更しない前提）
 * CEU設定が CB0Y0CR0Y1 (= UYVY) のため、デフォルトは UYVY。
 * もし「Y成分っぽいが、偶奇ピクセルが入れ替わって見える」場合は
 *   - YUV422_ORDER_UYVY_SWAP_Y
 *   - YUV422_ORDER_YUYV_SWAP_Y
 * を試してください（Yのみを入れ替えます）。
 */
#ifndef UDP_GRAYSCALE_YUV422_ORDER
#define UDP_GRAYSCALE_YUV422_ORDER YUV422_ORDER_UYVY_SWAP_Y
#endif

static const yuv422_order_t g_yuv422_order_fixed = (yuv422_order_t)UDP_GRAYSCALE_YUV422_ORDER;

/*
 * 4px束の順番補正（グレースケール送信のみ）
 * 0: 無効
 * 1: 4px(=4バイト)ごとに [0..1] と [2..3] を入れ替え (Y2 Y3 Y0 Y1 型)
 */
#ifndef UDP_GRAYSCALE_REORDER_4PX_MODE
#define UDP_GRAYSCALE_REORDER_4PX_MODE 1
#endif

static inline void reorder_grayscale_4px(uint8_t *buf, uint32_t n)
{
#if UDP_GRAYSCALE_REORDER_4PX_MODE == 1
    /* n is expected to be multiple of 4 (chunk_size=512 is OK) */
    for (uint32_t i = 0; i + 3U < n; i += 4U)
    {
        uint8_t a0 = buf[i];
        uint8_t a1 = buf[i + 1U];
        buf[i] = buf[i + 2U];
        buf[i + 1U] = buf[i + 3U];
        buf[i + 2U] = a0;
        buf[i + 3U] = a1;
    }
#else
    (void)buf;
    (void)n;
#endif
}

static void extract_y_from_yuv422(const uint8_t *yuv, uint8_t *y_out, uint32_t y_bytes, yuv422_order_t order)
{
    /* y_bytes must be even: 2 pixels at a time */
    for (uint32_t i = 0; i < y_bytes; i += 2)
    {
        uint32_t yuv_idx = i * 2U;
        if (order == YUV422_ORDER_UYVY)
        {
            /* [U0 Y0 V0 Y1] */
            y_out[i] = yuv[yuv_idx + 1];
            y_out[i + 1] = yuv[yuv_idx + 3];
        }
        else if (order == YUV422_ORDER_UYVY_SWAP_Y)
        {
            /* [U0 Y0 V0 Y1] but swap Y: (Y1,Y0) */
            y_out[i] = yuv[yuv_idx + 3];
            y_out[i + 1] = yuv[yuv_idx + 1];
        }
        else if (order == YUV422_ORDER_VYUY)
        {
            /* [V0 Y1 U0 Y0] */
            y_out[i] = yuv[yuv_idx + 3];
            y_out[i + 1] = yuv[yuv_idx + 1];
        }
        else if (order == YUV422_ORDER_YUYV_SWAP_Y)
        {
            /* [Y0 U0 Y1 V0] but swap Y: (Y1,Y0) */
            y_out[i] = yuv[yuv_idx + 2];
            y_out[i + 1] = yuv[yuv_idx + 0];
        }
        else
        {
            /* default YUYV: [Y0 U0 Y1 V0] */
            y_out[i] = yuv[yuv_idx + 0];
            y_out[i + 1] = yuv[yuv_idx + 2];
        }
    }

    reorder_grayscale_4px(y_out, y_bytes);
}

#define UDP_PORT_DEST 9000
#define FRAME_SIZE (320 * 240 * 2)    // YUV422 = 2 bytes/pixel
#define MONO_OFFSET FRAME_SIZE        // モノクロ画像オフセット
#define GRADIENT_OFFSET FRAME_SIZE    // p,q勾配マップオフセット（MONO_OFFSETと同じ位置）
#define DEPTH_OFFSET (FRAME_SIZE * 2) // 深度マップオフセット（8bit grayscale: 320×240 = 76,800バイト）

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

    /* HyperRAM base offset for the current video frame (snapshotted). */
    uint32_t frame_base_offset;
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

/* 静的UDP送信コンテキスト（ヒープ不要） */
static udp_send_ctx_t g_udp_send_ctx;

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
        /* Snapshot base at the start of each frame to avoid mid-frame base changes. */
        if (ctx->is_video_mode && (ctx->sent_bytes == 0U))
        {
            ctx->frame_base_offset = (uint32_t)g_video_frame_base_offset;
        }

        // 動画・写真データモード：512バイトずつ送信
        uint32_t remaining_bytes = ctx->photo_size - ctx->sent_bytes;
        send_size = (remaining_bytes < ctx->chunk_size) ? remaining_bytes : ctx->chunk_size;

        /* Y成分抽出は2ピクセル(=2バイト)単位で行うため偶数に丸める */
        send_size &= ~(size_t)1U;
#if UDP_GRAYSCALE_REORDER_4PX_MODE == 1
        /* 4px束並び替えを行う場合、4バイト境界に揃える */
        send_size &= ~(size_t)3U;
#endif

        // ヘッダー + データのサイズでバッファを確保
        size_t total_packet_size = sizeof(udp_photo_header_t) + send_size;
        p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)total_packet_size, PBUF_RAM);

        if (!p)
        {
            // pbuf確保失敗時は短い間隔でリトライ（間隔0でも安全）
            sys_timeout((ctx->interval_ms > 0) ? ctx->interval_ms : 1, udp_send_timer_cb, ctx);
            return;
        }

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

            // HyperRAMからYUV422データを読み込み、Y成分のみを抽出してグレースケール送信
            uint8_t *dest_ptr = (uint8_t *)p->payload + sizeof(udp_photo_header_t);

            // YUV422から必要なバイト数の2倍を読み込む（Y成分は2バイトごと）
            uint8_t yuv_buffer[1024]; // 一時バッファ（最大512バイトのグレースケール = 1024バイトのYUV422）
            uint32_t yuv_read_size = (uint32_t)(send_size * 2U);
            uint32_t yuv_offset = (uint32_t)(ctx->sent_bytes * 2U); // グレースケールオフセットをYUV422オフセットに変換

            if (yuv_read_size > sizeof(yuv_buffer))
            {
                /* 想定外（chunk_size変更など）: バッファに収まる範囲へ制限 */
                yuv_read_size = (uint32_t)sizeof(yuv_buffer);
                send_size = (size_t)(yuv_read_size / 2U);
            }

            uint32_t base = ctx->is_video_mode ? ctx->frame_base_offset : 0U;
            fsp_err_t read_err = hyperram_b_read(yuv_buffer, (void *)(base + yuv_offset), yuv_read_size);
            if (FSP_SUCCESS != read_err)
            {
                xprintf("[UDP] HyperRAM read error: %d\n", read_err);
                pbuf_free(p);
                sys_timeout((ctx->interval_ms > 0) ? ctx->interval_ms : 1, udp_send_timer_cb, ctx);
                return;
            }

            extract_y_from_yuv422(yuv_buffer, dest_ptr, (uint32_t)send_size, g_yuv422_order_fixed);

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

            // total_frames == UINT32_MAX (0xFFFFFFFF) なら無制限ループ
            bool is_unlimited = (ctx->total_frames == UINT32_MAX);

            if (is_unlimited || ctx->current_frame < ctx->total_frames)
            {
                // 次のフレームがある：フレーム間インターバルで待機
                ctx->sent_bytes = 0; // 次フレーム用にリセット
                ctx->frame_base_offset = (uint32_t)g_video_frame_base_offset;
                should_continue = true;
                next_interval = ctx->frame_interval_ms; // フレーム間は長めの間隔
                // ログ出力を削減（100フレームごと）
                if (ctx->current_frame % 100 == 0)
                {
                    if (is_unlimited)
                    {
                        xprintf("[VIDEO] F%u (unlimited) done\n", ctx->current_frame);
                    }
                    else
                    {
                        xprintf("[VIDEO] F%u/%u done\n", ctx->current_frame, ctx->total_frames);
                    }
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
        // 静的メモリなのでvPortFreeは不要
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

        /* 送信用コンテキストを静的メモリで使用 */
        udp_send_ctx_t *ctx = &g_udp_send_ctx;
        memset(ctx, 0, sizeof(*ctx));
        ctx->pcb = pcb;
        ctx->dest_ip = dest_ip;
        ctx->port = UDP_PORT_DEST;
        ctx->interval_ms = 0; /* 0ms間隔（lwIPタスクに余裕を持たせたい場合は3ms） */

        // 動画データ送信モード（グレースケール送信：Y成分のみ）
        ctx->is_video_mode = true;
        ctx->is_photo_mode = false;
        ctx->photo_data = (uint8_t *)HYPERRAM_BASE_ADDR; // 使用しない（hyperram_b_readで直接指定）
        ctx->photo_size = 320 * 240;                     // グレースケール: 320x240x1 = 76,800 bytes
        ctx->sent_bytes = 0;
        ctx->chunk_size = 512; // 512バイトずつ送信

        // マルチフレーム設定
        ctx->current_frame = 0;
        ctx->total_frames = UINT32_MAX; // 無制限フレーム送信
        ctx->frame_interval_ms = 2;     // フレーム間2ms待機（thread0と同期、高速化）
        ctx->is_frame_complete = false;
        ctx->frame_base_offset = (uint32_t)g_video_frame_base_offset;

        xprintf("[VIDEO] Starting grayscale transmission (Y component): %d bytes/frame, %d chunks/frame\n",
                ctx->photo_size, (ctx->photo_size + ctx->chunk_size - 1) / ctx->chunk_size);

        /* 1発目をスケジュール（ネットワーク安定化のため500ms待機） */
        sys_timeout(500, udp_send_timer_cb, ctx);
    }

forever:
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
