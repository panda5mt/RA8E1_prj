#include "hyperram_integ.h"
#include "main_thread1.h"
#include "putchar_ra8usb.h"
#include "hal_data.h"

#include "r_ospi_b.h"
#include "r_spi_flash_api.h"
#include <string.h>
#include <stdint.h>
#include "verify_mode.h"
#include "FreeRTOS.h"
#include "semphr.h"

/*
 * Debug aid: verify memory-mapped HyperRAM writes by immediate read-back.
 * Enabled by default in FFT verification mode to help quantify/mitigate persistent mismatches.
 */
#ifndef HYPERRAM_WRITE_VERIFY
#if defined(APP_MODE_FFT_VERIFY) && (APP_MODE_FFT_VERIFY != 0)
#define HYPERRAM_WRITE_VERIFY 1
#else
#define HYPERRAM_WRITE_VERIFY 1
#endif
#endif

#ifndef HYPERRAM_WRITE_VERIFY_RETRIES
/* Number of re-write attempts after the initial write when mismatch is detected. */
#define HYPERRAM_WRITE_VERIFY_RETRIES 1
#endif

#ifndef HYPERRAM_WRITE_VERIFY_LOG
/* 0: silent (preferred). 1: print per-call summary when retries/failures occurred. */
#define HYPERRAM_WRITE_VERIFY_LOG 0
#endif

/*
 * EXPERIMENT (unsafe): increase the memory-mapped RW chunk size.
 *
 * IMPORTANT:
 * This project uses an OSPI address conversion that is effectively 16-byte granular.
 * The safe path never crosses a 16-byte boundary within one contiguous RW chunk.
 *
 * To intentionally test what happens when we do cross boundaries (potentially faster
 * but may corrupt/interleave data), enable the following switch.
 */
#ifndef HYPERRAM_UNSAFE_RW_CROSS_16B
/*
 * IMPORTANT:
 * The OSPI address conversion used by this project is 16-byte granular.
 * Crossing a 16-byte boundary within one contiguous memory-mapped RW chunk can
 * corrupt/interleave data (often seen as repeating patterns or speckle).
 *
 * Keep this disabled for correctness; enable only for controlled experiments.
 */
#define HYPERRAM_UNSAFE_RW_CROSS_16B 1
#endif

#ifndef HYPERRAM_RW_CHUNK_SIZE
#if HYPERRAM_UNSAFE_RW_CROSS_16B
#define HYPERRAM_RW_CHUNK_SIZE 64U
#else
#define HYPERRAM_RW_CHUNK_SIZE 16U
#endif
#endif

/*
 * When verifying memory-mapped writes, optionally wait for the OSPI mmap engine
 * to become idle before reading back / retrying.
 * This adds little overhead in the common case, but can reduce rare persistent
 * mismatches (fail=1) under high bus contention.
 */
#ifndef HYPERRAM_WV_WAIT_MMAP_IDLE
#if HYPERRAM_UNSAFE_RW_CROSS_16B
#define HYPERRAM_WV_WAIT_MMAP_IDLE 1
#else
#define HYPERRAM_WV_WAIT_MMAP_IDLE 0
#endif
#endif

/* Flash device timing */
#define OSPI_B_TIME_UNIT (BSP_DELAY_UNITS_MICROSECONDS)
#define OSPI_B_TIME_RESET_SETUP (2U)    /*  Type 50ns */
#define OSPI_B_TIME_RESET_PULSE (1000U) /*  Type 500us */

spi_flash_direct_transfer_t g_ospi0_trans;
bool ospi_b_dma_sent = false;
static bool g_ospi_initialized = false;

/* HyperRAMスレッドセーフアクセス管理（ミューテックスベース） */
static SemaphoreHandle_t g_hyperram_mutex = NULL;

/* HyperRAM write verify/retry counters (for diagnostics). */
static volatile uint32_t g_hyperram_wv_mismatch_chunks = 0;
static volatile uint32_t g_hyperram_wv_retries = 0;
static volatile uint32_t g_hyperram_wv_failed_chunks = 0;

/* Detailed per-chunk WV classification counters (diagnostics). */
static volatile uint32_t g_hyperram_wv_chunks_mismatched = 0;
static volatile uint32_t g_hyperram_wv_chunks_retry_ok = 0;
static volatile uint32_t g_hyperram_wv_chunks_safe_fallback_used = 0;

#if HYPERRAM_WRITE_VERIFY && HYPERRAM_UNSAFE_RW_CROSS_16B
static bool hyperram_wv_safe_rewrite_verify(const uint8_t *src,
                                            volatile uint8_t *dst8,
                                            uint32_t write_size)
{
    uint32_t sub_offset = 0;

    while (sub_offset < write_size)
    {
        uint32_t sub_remaining = write_size - sub_offset;
        uintptr_t base_addr = (uintptr_t)(dst8 + sub_offset);
        uint32_t in_block = (uint32_t)(base_addr & 0x0FU);
        uint32_t to_block_end = 16U - in_block;
        uint32_t sub_size = sub_remaining;

        if (sub_size > to_block_end)
        {
            sub_size = to_block_end;
        }

        const uint8_t *sub_src = src + sub_offset;
        volatile uint8_t *sub_dst8 = dst8 + sub_offset;

        if ((((uintptr_t)sub_src | (uintptr_t)sub_dst8 | (uintptr_t)sub_size) & 0x3u) == 0u)
        {
            const uint32_t *src32 = (const uint32_t *)sub_src;
            volatile uint32_t *dst32 = (volatile uint32_t *)sub_dst8;
            uint32_t words = sub_size >> 2;
            for (uint32_t i = 0; i < words; i++)
            {
                dst32[i] = src32[i];
            }
        }
        else
        {
            for (uint32_t i = 0; i < sub_size; i++)
            {
                sub_dst8[i] = sub_src[i];
            }
        }

#if HYPERRAM_WV_WAIT_MMAP_IDLE
        ospi_wait_mmap_idle();
#endif
        __DSB();
        __ISB();

        uint32_t rb32_aligned[(16u + 3u) / 4u];
        uint8_t *rb = (uint8_t *)rb32_aligned;
        if ((((uintptr_t)sub_dst8 | (uintptr_t)sub_size) & 0x3u) == 0u)
        {
            const volatile uint32_t *src32 = (const volatile uint32_t *)sub_dst8;
            uint32_t *rb32 = rb32_aligned;
            uint32_t words = sub_size >> 2;
            for (uint32_t i = 0; i < words; i++)
            {
                rb32[i] = src32[i];
            }
        }
        else
        {
            for (uint32_t i = 0; i < sub_size; i++)
            {
                rb[i] = sub_dst8[i];
            }
        }

        for (uint32_t i = 0; i < sub_size; i++)
        {
            if (rb[i] != sub_src[i])
            {
                return false;
            }
        }

        sub_offset += sub_size;
    }

    return true;
}
#endif
spi_flash_erase_command_t g_command_erase_sets[] =
    {
        [0] = {
            .command = 0x00,
            .size = 0U,
        }};

/* Custom command sets. */
ospi_b_xspi_command_set_t g_command_sets[] =
    {

        /* 8D-8D-8D example with inverted lower command byte. */
        [0] = {.protocol = SPI_FLASH_PROTOCOL_8D_8D_8D,
               .latency_mode = OSPI_B_LATENCY_MODE_VARIABLE,
               .frame_format = OSPI_B_FRAME_FORMAT_XSPI_PROFILE_1,
               .command_bytes = OSPI_RAM_COMMAND_BYTES,
               .address_bytes = SPI_FLASH_ADDRESS_BYTES_4,
               .read_command = OSPI_B_COMMAND_READ,
               .program_command = OSPI_B_COMMAND_WRITE,
               .write_enable_command = OSPI_B_COMMAND_WRITE_ENABLE,
               .status_command = 0x00,
               //.status_needs_address = false,
               //.status_address_bytes = 0,
               //.address_msb_mask = 0xE0,
               .read_dummy_cycles = OSPI_RAM_READ_LATENCY_CYCLES,
               .program_dummy_cycles = OSPI_RAM_WRITE_LATENCY_CYCLES,

               .status_dummy_cycles = 0,
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

// ★ OSPI レジスタの要点ダンプ（ch=0/1 の両方見ます）
void ospi_dump_regs(void)
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

void ospi_wait_mmap_idle(void)
{
    const uint32_t BUSY_MASK = (0x03u << R_XSPI0_COMSTT_MEMACCCH_Pos);
    while (R_XSPI0->COMSTT & BUSY_MASK)
    {
        __NOP();
    }
}

void dump_ospi_read_side(R_XSPI0_Type *r, int ch)
{
    uint32_t cm0 = r->CMCFGCS[ch].CMCFG0;
    uint32_t cm1 = r->CMCFGCS[ch].CMCFG1;
    uint32_t liocfg = r->LIOCFGCS[ch];
    uint32_t wrap = r->WRAPCFG;

    uint32_t rdcmd = (cm1 >> R_XSPI0_CMCFGCS_CMCFG1_RDCMD_Pos) & 0xFFFF;
    uint32_t rdlate = (cm1 >> R_XSPI0_CMCFGCS_CMCFG1_RDLATE_Pos) & 0xFF;
    uint32_t addsz = (cm0 >> R_XSPI0_CMCFGCS_CMCFG0_ADDSIZE_Pos) & 0x3;
    uint32_t ffmt = (cm0 >> R_XSPI0_CMCFGCS_CMCFG0_FFMT_Pos) & 0x7;
    uint32_t latemd = (liocfg >> R_XSPI0_LIOCFGCS_LATEMD_Pos) & 0x1;
    uint32_t dssft0 = (wrap >> R_XSPI0_WRAPCFG_DSSFTCS0_Pos) & 0x1F;
    uint32_t dssft1 = (wrap >> R_XSPI0_WRAPCFG_DSSFTCS1_Pos) & 0x1F;

    xprintf("RDCMD=0x%04x RDLATE=%u ADDRSIZE=%u FFMT=%u\n",
            (unsigned long)rdcmd, (unsigned long)rdlate, (unsigned long)addsz,
            (unsigned long)ffmt);

    xprintf("LATEMD=%u DSSFT0=%u DSSFT1=%u\n",
            (unsigned long)latemd, (unsigned long)dssft0, (unsigned long)dssft1);

    uint32_t cmcfg0 = r->CMCFGCS[ch].CMCFG0;
    bool addr_replace_enabled =
        (cmcfg0 & R_XSPI0_CMCFGCS_CMCFG0_ADDRPEN_Msk) != 0;

    xprintf("CMCFG0[%d] = 0x%08X\n", ch, (unsigned long)cmcfg0);
    xprintf("ADDRPEN (Address Replace) = %s\n",
            addr_replace_enabled ? "ENABLED" : "DISABLED");
}

fsp_err_t hyperram_init(void)
{
    fsp_err_t err = FSP_SUCCESS;
    if (g_ospi_initialized)
    {
        // すでに初期化済み
        return FSP_SUCCESS;
    }

    R_BSP_MODULE_START(FSP_IP_OSPI, 1);
    // 0. VCCによるHW設定 VCC2 = 1.8VなのでLVOCR.LVO1E=1にする
    uint32_t *lvocr_ptr = (uint32_t *)0x4001E000;
    xprintf("[SYSTEM] LVOCR = 0x%02x\n", *lvocr_ptr);
    *lvocr_ptr = 0x0001;
    xprintf("[SYSTEM] LVOCR = 0x%02x\n", *lvocr_ptr);

    // 1. OSPI 初期化
    /* Reset flash device by driving OM_RESET pin */
    R_XSPI0->LIOCTL_b.RSTCS1 = 0;
    R_BSP_SoftwareDelay(OSPI_B_TIME_RESET_PULSE, OSPI_B_TIME_UNIT);
    R_XSPI0->LIOCTL_b.RSTCS1 = 1;
    R_BSP_SoftwareDelay(OSPI_B_TIME_RESET_SETUP, OSPI_B_TIME_UNIT);

    // SCB_InvalidateDCache_by_Addr((uint8_t *)HYPERRAM_BASE_ADDR, 256 * 256 * 2);

    err = R_OSPI_B_Open(&g_ospi0_ctrl, &g_ospi0_cfg);
    if (FSP_SUCCESS != err)
    {
        //__BKPT(); // 初期化失敗
        xprintf("[OSPI] init error!%d\n", err);
        return err;
    }

    err = R_OSPI_B_SpiProtocolSet(&g_ospi0_ctrl, SPI_FLASH_PROTOCOL_8D_8D_8D);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] set protocol error!:%d\n", err);
        return err;
    }

    xprintf("[OSPI] init Ok\n");

    R_XSPI0->WRAPCFG_b.DSSFTCS1 = 1U;

    // /* Configure DDR sampling window extend */
    R_XSPI0->LIOCFGCS_b[1].DDRSMPEX = 4U;

    // default CR = 0x52F0(LE) -> 0xF052(BE) (Normal Operation, 24ohm, no DQSM pre-cycle, 8-clock latency, variable latency, 32bytes burst)
    // write   CR = 0xC051(BE) -> 0x51C0(LE) (Normal Operation, 34ohm, no DQSM pre-cycle, 8-clock latency, variable latency, 64bytes burst)
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE_REGISTER, OSPI_RAM_COMMAND_BYTES,
                         0x00040000, 4,
                         0x51C0, 2,
                         0, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);

    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }

    // read ID
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ_ID, OSPI_RAM_COMMAND_BYTES,
                         0x00000000, 4,
                         0x00, 2,
                         OSPI_RAM_READ_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }
    xprintf("ID=0x%04x\n", g_ospi0_trans.data);

    // read CR
    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ_REGISTER, OSPI_RAM_COMMAND_BYTES,
                         0x00040000, 4,
                         0x00, 2,
                         OSPI_RAM_READ_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        xprintf("[OSPI] direct transfer error!\n");
        return err;
    }
    xprintf("CR=0x%04x\n", g_ospi0_trans.data);

    // アドレス変換
    R_XSPI0->CMCFGCS[1].CMCFG0_b.ARYAMD = 1; // Array address mode

    // 正常終了
    xprintf("[OSPI] RW init end\n");

    g_ospi_initialized = true;

    // スレッドセーフアクセス用ミューテックス作成（優先度継承付き）
    if (g_hyperram_mutex == NULL)
    {
        g_hyperram_mutex = xSemaphoreCreateMutex();
        if (g_hyperram_mutex == NULL)
        {
            xprintf("[HyperRAM] ERROR: Mutex creation failed!\n");
            return FSP_ERR_OUT_OF_MEMORY;
        }
        xprintf("[HyperRAM] Mutex-based thread-safe access initialized\n");
    }

#if defined(APP_MODE_FFT_VERIFY) && (APP_MODE_FFT_VERIFY != 0)
    xprintf("[HyperRAM] mmap RW chunk=%dB cross16=%d\n",
            (int)HYPERRAM_RW_CHUNK_SIZE,
            (int)HYPERRAM_UNSAFE_RW_CROSS_16B);
#endif

    return err;
}

/* Note: Mutex is created in hyperram_timing_optimization() */
fsp_err_t hyperram_word_write(uint32_t addr, uint32_t data)
{
    fsp_err_t err = FSP_SUCCESS;

    /* Keep semantics consistent with hyperram_b_read/write: addr is a logical offset. */
    if ((addr & 0x0FU) > 0x0CU)
    {
        /* This 4-byte access would cross a 16-byte address-conversion block. */
        xprintf("[HyperRAM-W] ERROR: word_write crosses 16B boundary (addr=0x%08lx)\n", (unsigned long)addr);
        return FSP_ERR_INVALID_ARGUMENT;
    }

    uint32_t converted_addr = addr;

    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_WRITE, OSPI_RAM_COMMAND_BYTES,
                         converted_addr, 4,
                         data, 4,
                         OSPI_RAM_WRITE_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    return err;
}

uint32_t hyperram_word_read(uint32_t addr)
{
    fsp_err_t err = FSP_SUCCESS;

    /* Keep semantics consistent with hyperram_b_read/write: addr is a logical offset. */
    if ((addr & 0x0FU) > 0x0CU)
    {
        /* This 4-byte access would cross a 16-byte address-conversion block. */
        xprintf("[HyperRAM-R] ERROR: word_read crosses 16B boundary (addr=0x%08lx)\n", (unsigned long)addr);
        return 0xFFFFFFFFu;
    }

    uint32_t converted_addr = addr;

    err = ospi_raw_trans(&g_ospi0_trans,
                         OSPI_B_COMMAND_READ, OSPI_RAM_COMMAND_BYTES,
                         converted_addr, 4,
                         0x00, 4,
                         OSPI_RAM_READ_LATENCY_CYCLES, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);

    if (FSP_SUCCESS != err)
    {
        xprintf("[HyperRAM-R] ERROR: word_read failed (err=%d addr=0x%08lx)\n", (int)err, (unsigned long)addr);
        return 0xFFFFFFFFu;
    }

    return g_ospi0_trans.data;
}

fsp_err_t hyperram_b_write(const void *p_src, void *p_dest, uint32_t total_length)
{
    /* Preserve legacy behavior: block up to 5 seconds. */
    return hyperram_b_write_timed(p_src, p_dest, total_length, pdMS_TO_TICKS(5000));
}

fsp_err_t hyperram_b_write_timed(const void *p_src, void *p_dest, uint32_t total_length, TickType_t wait_ticks)
{
    fsp_err_t err = FSP_SUCCESS;

    if (g_hyperram_mutex == NULL)
    {
        xprintf("[HyperRAM-W] ERROR: Mutex not initialized!\n");
        return FSP_ERR_NOT_INITIALIZED;
    }

    /* Mutex acquire: caller-controlled wait. */
    if (xSemaphoreTake(g_hyperram_mutex, wait_ticks) != pdTRUE)
    {
        return FSP_ERR_TIMEOUT;
    }

    // 排他制御下で書き込み実行
    const uint8_t *src_p8 = (const uint8_t *)p_src;
    uint8_t *dest_p8 = (uint8_t *)p_dest;
    /*
     * Address conversion is 16-byte granular:
     *   converted = ((addr & ~0xF) << 6) | (addr & 0xF)
     * Copying/writing 64 bytes contiguously would span 4 interleaved 16-byte blocks,
     * which can manifest as 4x repeating patterns in image data.
     */
    const uint32_t batch_size = (uint32_t)HYPERRAM_RW_CHUNK_SIZE;
    uint32_t offset = 0;

#if HYPERRAM_WRITE_VERIFY
    uint32_t verify_retries_used = 0;
    uint32_t verify_mismatch_chunks = 0;
    uint32_t verify_failed_chunks = 0;

    uint32_t verify_chunks_mismatched = 0;
    uint32_t verify_chunks_retry_ok = 0;
    uint32_t verify_chunks_safe_fallback_used = 0;

#if HYPERRAM_WV_WAIT_MMAP_IDLE
    /*
     * Adaptive mitigation:
     * If we see a mismatch, enable a short "mmap idle wait" window for the
     * subsequent chunks. This reduces bursty mismatches caused by reading back
     * too early under contention, without paying the wait cost all the time.
     */
    uint32_t verify_wait_budget = 0;
#endif
#endif

    while (offset < total_length)
    {
        uint32_t remaining = total_length - offset;
        uint32_t base_addr = (uint32_t)dest_p8 + offset;
        uint32_t in_block = base_addr & 0x0FU;
        uint32_t to_block_end = 16U - in_block;
        uint32_t write_size = remaining;

        if (write_size > batch_size)
        {
            write_size = batch_size;
        }

#if !HYPERRAM_UNSAFE_RW_CROSS_16B
        if (write_size > to_block_end)
        {
            /* Do not cross a 16-byte address-conversion block. */
            write_size = to_block_end;
        }
#else
        (void)to_block_end;
#endif

        uint32_t adr = (uint32_t)dest_p8 + offset;
        adr += (uint32_t)HYPERRAM_BASE_ADDR;

        /*
         * IMPORTANT:
         * Avoid memcpy() here. Depending on the libc implementation and optimization level,
         * memcpy may generate byte/halfword accesses and/or unaligned transfers. Some OSPI
         * memory-mapped devices/controllers behave poorly with sub-word writes, leading to
         * persistent read-back mismatches.
         *
         * Prefer 32-bit aligned volatile accesses when possible; otherwise fall back to
         * explicit byte-wise volatile writes.
         */
        const uint8_t *src = (const uint8_t *)(src_p8 + offset);
        volatile uint8_t *dst8 = (volatile uint8_t *)adr;

#if HYPERRAM_WRITE_VERIFY
        uint32_t attempt = 0;
        bool ok = false;
        bool wait_before_verify = false;

        bool chunk_mismatched = false;
        bool chunk_used_safe_fallback = false;

        while (true)
        {
            /* Write the chunk. */
            if ((((uintptr_t)src | (uintptr_t)dst8 | (uintptr_t)write_size) & 0x3u) == 0u)
            {
                const uint32_t *src32 = (const uint32_t *)src;
                volatile uint32_t *dst32 = (volatile uint32_t *)dst8;
                uint32_t words = write_size >> 2;
                for (uint32_t i = 0; i < words; i++)
                {
                    dst32[i] = src32[i];
                }
            }
            else
            {
                for (uint32_t i = 0; i < write_size; i++)
                {
                    dst8[i] = src[i];
                }
            }

#if HYPERRAM_WV_WAIT_MMAP_IDLE
            /*
             * Wait policy:
             * - Always wait before verify on a retry attempt.
             * - Additionally, after any mismatch, wait for a short window of
             *   subsequent chunks (verify_wait_budget) to reduce mismatch bursts.
             */
            if (wait_before_verify
#if HYPERRAM_WRITE_VERIFY
                || (verify_wait_budget != 0u && attempt == 0u)
#endif
            )
            {
                ospi_wait_mmap_idle();
                wait_before_verify = false;
#if HYPERRAM_WRITE_VERIFY
                if ((verify_wait_budget != 0u) && (attempt == 0u))
                {
                    verify_wait_budget--;
                }
#endif
            }
#endif
            /* Ensure writes reach the memory-mapped window before we verify. */
            __DSB();
            __ISB();

            /* Read-back and compare. */
            uint32_t rb32_aligned[(HYPERRAM_RW_CHUNK_SIZE + 3u) / 4u];
            uint8_t *rb = (uint8_t *)rb32_aligned;

            if ((((uintptr_t)dst8 | (uintptr_t)write_size) & 0x3u) == 0u)
            {
                const volatile uint32_t *src32 = (const volatile uint32_t *)dst8;
                uint32_t *rb32 = rb32_aligned;
                uint32_t words = write_size >> 2;
                for (uint32_t i = 0; i < words; i++)
                {
                    rb32[i] = src32[i];
                }
            }
            else
            {
                for (uint32_t i = 0; i < write_size; i++)
                {
                    rb[i] = dst8[i];
                }
            }

            ok = true;
            for (uint32_t i = 0; i < write_size; i++)
            {
                if (rb[i] != src[i])
                {
                    ok = false;
                    break;
                }
            }

            if (ok)
            {
                if (chunk_mismatched && (attempt != 0u) && !chunk_used_safe_fallback)
                {
                    verify_chunks_retry_ok++;
                }
                break;
            }

            verify_mismatch_chunks++;
            if (!chunk_mismatched)
            {
                chunk_mismatched = true;
                verify_chunks_mismatched++;
            }

            wait_before_verify = true;
#if HYPERRAM_WV_WAIT_MMAP_IDLE
            /* Enable short wait window for subsequent chunks. */
            verify_wait_budget = 32u;
#endif
            if (attempt >= (uint32_t)HYPERRAM_WRITE_VERIFY_RETRIES)
            {
#if HYPERRAM_UNSAFE_RW_CROSS_16B
                /*
                 * Do not increase retry count further; instead, fall back to a
                 * safe 16-byte non-crossing rewrite+verify for this chunk.
                 */
                chunk_used_safe_fallback = true;
                verify_chunks_safe_fallback_used++;
                if (hyperram_wv_safe_rewrite_verify(src, dst8, write_size))
                {
                    ok = true;
                    break;
                }
#endif
                verify_failed_chunks++;
                break;
            }

            attempt++;
            verify_retries_used++;
        }
#else
        if ((((uintptr_t)src | (uintptr_t)dst8 | (uintptr_t)write_size) & 0x3u) == 0u)
        {
            const uint32_t *src32 = (const uint32_t *)src;
            volatile uint32_t *dst32 = (volatile uint32_t *)dst8;
            uint32_t words = write_size >> 2;
            for (uint32_t i = 0; i < words; i++)
            {
                dst32[i] = src32[i];
            }
        }
        else
        {
            for (uint32_t i = 0; i < write_size; i++)
            {
                dst8[i] = src[i];
            }
        }
#endif
        offset += write_size;
    }

    /* Ensure all posted writes to the memory-mapped window complete. */
    __DSB();
    __ISB();

#if HYPERRAM_WRITE_VERIFY
    /* Accumulate into global counters under the same mutex protection. */
    g_hyperram_wv_mismatch_chunks += verify_mismatch_chunks;
    g_hyperram_wv_retries += verify_retries_used;
    g_hyperram_wv_failed_chunks += verify_failed_chunks;

    g_hyperram_wv_chunks_mismatched += verify_chunks_mismatched;
    g_hyperram_wv_chunks_retry_ok += verify_chunks_retry_ok;
    g_hyperram_wv_chunks_safe_fallback_used += verify_chunks_safe_fallback_used;
#endif

#if HYPERRAM_WRITE_VERIFY && HYPERRAM_WRITE_VERIFY_LOG
    if ((verify_retries_used != 0u) || (verify_failed_chunks != 0u))
    {
        xprintf("[HyperRAM-W] verify: retries=%d mismatch_chunks=%d\n failed_chunks=%d len=%d\n",
                (int)verify_retries_used,
                (int)verify_mismatch_chunks,
                (int)verify_failed_chunks,
                (int)total_length);
    }
#elif HYPERRAM_WRITE_VERIFY
    (void)verify_retries_used;
    (void)verify_mismatch_chunks;
    (void)verify_failed_chunks;
#endif

    // ミューテックス解放
    xSemaphoreGive(g_hyperram_mutex);
    return err;
}

void hyperram_write_verify_counters_reset(void)
{
    /* Best-effort reset; ok if a write is in-flight (diagnostics only). */
    g_hyperram_wv_mismatch_chunks = 0;
    g_hyperram_wv_retries = 0;
    g_hyperram_wv_failed_chunks = 0;

    g_hyperram_wv_chunks_mismatched = 0;
    g_hyperram_wv_chunks_retry_ok = 0;
    g_hyperram_wv_chunks_safe_fallback_used = 0;
}

void hyperram_write_verify_counters_get(uint32_t *p_mismatch_chunks,
                                        uint32_t *p_retries,
                                        uint32_t *p_failed_chunks)
{
    if (p_mismatch_chunks)
    {
        *p_mismatch_chunks = g_hyperram_wv_mismatch_chunks;
    }
    if (p_retries)
    {
        *p_retries = g_hyperram_wv_retries;
    }
    if (p_failed_chunks)
    {
        *p_failed_chunks = g_hyperram_wv_failed_chunks;
    }
}

uint32_t hyperram_write_verify_is_enabled(void)
{
    return (uint32_t)HYPERRAM_WRITE_VERIFY;
}

uint32_t hyperram_write_verify_retries(void)
{
    return (uint32_t)HYPERRAM_WRITE_VERIFY_RETRIES;
}

fsp_err_t hyperram_b_read(void *p_dest, const void *p_src, uint32_t total_length)
{
    /* Preserve legacy behavior: block up to 5 seconds. */
    return hyperram_b_read_timed(p_dest, p_src, total_length, pdMS_TO_TICKS(5000));
}

fsp_err_t hyperram_b_read_timed(void *p_dest, const void *p_src, uint32_t total_length, TickType_t wait_ticks)
{
    fsp_err_t err = FSP_SUCCESS;

    if (g_hyperram_mutex == NULL)
    {
        xprintf("[HyperRAM-R] ERROR: Mutex not initialized!\n");
        return FSP_ERR_NOT_INITIALIZED;
    }

    /* Mutex acquire: caller-controlled wait. */
    if (xSemaphoreTake(g_hyperram_mutex, wait_ticks) != pdTRUE)
    {
        return FSP_ERR_TIMEOUT;
    }

    // 排他制御下で読み込み実行
    uint8_t *dest_p8 = (uint8_t *)p_dest;
    const uint8_t *src_p8 = (const uint8_t *)p_src;
    uint32_t remaining_size = total_length;
    uint32_t current_offset = 0;

    while (remaining_size > 0)
    {
        /*
         * Address conversion is 16-byte granular. Never cross a 16-byte boundary
         * within one memcpy, otherwise data order becomes interleaved.
         */
        uint32_t base_addr = (uint32_t)src_p8 + current_offset;
        uint32_t in_block = base_addr & 0x0FU;
        uint32_t to_block_end = 16U - in_block;
        uint32_t read_size = remaining_size;

        if (read_size > (uint32_t)HYPERRAM_RW_CHUNK_SIZE)
        {
            read_size = (uint32_t)HYPERRAM_RW_CHUNK_SIZE;
        }

#if !HYPERRAM_UNSAFE_RW_CROSS_16B
        if (read_size > to_block_end)
        {
            read_size = to_block_end;
        }
#else
        (void)to_block_end;
#endif

        uint32_t converted_addr = base_addr;
        const volatile uint8_t *src8 = (const volatile uint8_t *)((uintptr_t)HYPERRAM_BASE_ADDR + (uintptr_t)converted_addr);
        uint8_t *dst = (uint8_t *)(dest_p8 + current_offset);

        /* Prefer 32-bit aligned volatile loads when possible; else byte loads. */
        if ((((uintptr_t)src8 | (uintptr_t)dst | (uintptr_t)read_size) & 0x3u) == 0u)
        {
            const volatile uint32_t *src32 = (const volatile uint32_t *)src8;
            uint32_t *dst32 = (uint32_t *)dst;
            uint32_t words = read_size >> 2;
            for (uint32_t i = 0; i < words; i++)
            {
                dst32[i] = src32[i];
            }
        }
        else
        {
            for (uint32_t i = 0; i < read_size; i++)
            {
                dst[i] = src8[i];
            }
        }
        current_offset += read_size;
        remaining_size -= read_size;
    }

    /* Serialize subsequent code vs. memory-mapped reads. */
    __DSB();
    __ISB();

    // ミューテックス解放
    xSemaphoreGive(g_hyperram_mutex);
    return err;
}

void ospi_dmac_cb(transfer_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    ospi_b_dma_sent = true;
    // xprintf("OSPI DMAC transfer done.\n");
}

void hyperram_write_verify_detail_get(uint32_t *p_chunks_mismatched,
                                      uint32_t *p_retry_ok_chunks,
                                      uint32_t *p_safe_fallback_used_chunks)
{
    if (p_chunks_mismatched)
    {
        *p_chunks_mismatched = g_hyperram_wv_chunks_mismatched;
    }
    if (p_retry_ok_chunks)
    {
        *p_retry_ok_chunks = g_hyperram_wv_chunks_retry_ok;
    }
    if (p_safe_fallback_used_chunks)
    {
        *p_safe_fallback_used_chunks = g_hyperram_wv_chunks_safe_fallback_used;
    }
}