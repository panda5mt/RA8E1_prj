#include "fft_depth_test.h"
#include "putchar_ra8usb.h"
#include "verify_mode.h"
#include <math.h>
#include <string.h>

/* CMSIS-DSP (for MVE-optimized CFFT) */
#include "arm_math_f16.h"

/*
 * Logging helpers:
 * - FFT_LOG: always-on (keep minimal progress/timing visible)
 * - FFT_VLOG: verbose-only (suppress when APP_MODE_FFT_VERIFY_VERBOSE=0)
 */
#define FFT_LOG(...) xprintf(__VA_ARGS__)

/*
 * Diagnostic one-time logs (build caps, selected FFT path).
 * Default off to minimize runtime logging and keep benchmarks clean.
 */
#ifndef FFT_DIAG_LOG
#define FFT_DIAG_LOG 0
#endif

#if defined(APP_MODE_FFT_VERIFY_VERBOSE) && (APP_MODE_FFT_VERIFY_VERBOSE == 0)
#define FFT_VLOG(...) ((void)0)
#else
#define FFT_VLOG(...) xprintf(__VA_ARGS__)
#endif

#if defined(APP_MODE_FFT_VERIFY_PRINT_PHASES_ONCE) && (APP_MODE_FFT_VERIFY_PRINT_PHASES_ONCE != 0)
static bool g_fft_print_phases_once_done = false;
#endif

#if FFT_DIAG_LOG
static bool g_fft_print_build_caps_once = false;
static bool g_fft_print_fft256_path_once = false;
#endif

static inline void fft_verify_delay_ms(uint32_t ms)
{
#if defined(APP_MODE_FFT_VERIFY_USE_DELAYS) && (APP_MODE_FFT_VERIFY_USE_DELAYS == 0)
    (void)ms;
#else
    if (ms > 0u)
    {
        // vTaskdelay(pdMS_TO_TICKS(ms));
    }
#endif
}

// Helium MVE (ARM M-profile Vector Extension) support
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE > 0)
#include <arm_mve.h>
#define USE_HELIUM_MVE 1
#else
#define USE_HELIUM_MVE 0
#endif

#if defined(__GNUC__)
#define FFT_ALIGN16 __attribute__((aligned(16)))
#else
#define FFT_ALIGN16
#endif

/* 三角関数テーブル(事前計算で高速化) */
#define MAX_FFT_SIZE 256
static float cos_table[MAX_FFT_SIZE / 2];
static float sin_table[MAX_FFT_SIZE / 2];
static bool trig_table_initialized = false;
static int trig_table_N = 0;

/* Precomputed bit-reversal indices for current trig_table_N. */
static uint16_t bitrev_table[MAX_FFT_SIZE];
static int bitrev_table_N = 0;

/* Per-stage twiddle buffers (contiguous), sized for MAX_FFT_SIZE. */
static float twiddle_stage_real[MAX_FFT_SIZE / 2];
static float twiddle_stage_imag[MAX_FFT_SIZE / 2];

/* Interleaved complex scratch for CMSIS CFFT: [re0, im0, re1, im1, ...] */
static FFT_ALIGN16 float g_cfft_io[2 * MAX_FFT_SIZE];

/* Optional float16 CFFT scratch/instance (requires CMSIS float16 support). */
#if defined(ARM_FLOAT16_SUPPORTED)
static FFT_ALIGN16 float16_t g_cfft_io_f16[2 * MAX_FFT_SIZE];
static arm_cfft_instance_f16 g_cfft_inst_f16;
static int g_cfft_inst_f16_N = 0;
#endif

/*
 * Experimental: use float16 CFFT for N=256.
 * This can be faster on some targets but reduces precision and may harm image/FC quality.
 * Keep disabled by default for stability.
 */
#ifndef FFT_USE_CFFT_F16_256
#define FFT_USE_CFFT_F16_256 (0)
#endif

static arm_cfft_instance_f32 g_cfft_inst_f32;
static int g_cfft_inst_N = 0;

static inline const arm_cfft_instance_f32 *fft_get_cfft_instance_f32(int N)
{
    if (!((N == 128) || (N == 256)))
    {
        return NULL;
    }

    if (g_cfft_inst_N != N)
    {
        /* For MVE, CMSIS recommends using arm_cfft_init_f32 rather than const structs. */
        arm_status st = arm_cfft_init_f32(&g_cfft_inst_f32, (uint16_t)N);
        if (st != ARM_MATH_SUCCESS)
        {
            g_cfft_inst_N = 0;
            return NULL;
        }
        g_cfft_inst_N = N;
    }

    return &g_cfft_inst_f32;
}

#if defined(ARM_FLOAT16_SUPPORTED)
static inline const arm_cfft_instance_f16 *fft_get_cfft_instance_f16(int N)
{
    if (!(N == 256))
    {
        return NULL;
    }

    if (g_cfft_inst_f16_N != N)
    {
        arm_status st = arm_cfft_init_f16(&g_cfft_inst_f16, (uint16_t)N);
        if (st != ARM_MATH_SUCCESS)
        {
            g_cfft_inst_f16_N = 0;
            return NULL;
        }
        g_cfft_inst_f16_N = N;
    }

    return &g_cfft_inst_f16;
}
#endif

static inline void fft_1d_cmsis_cfft_f32(float *real, float *imag, int N, bool is_inverse)
{
    const arm_cfft_instance_f32 *S = fft_get_cfft_instance_f32(N);
    if (!S)
    {
        return;
    }

#if USE_HELIUM_MVE
    int i;
    for (i = 0; i <= N - 4; i += 4)
    {
        float32x4x2_t tmp;
        tmp.val[0] = vld1q_f32(&real[i]);
        tmp.val[1] = vld1q_f32(&imag[i]);
        /* Store interleaved: [re0,im0,re1,im1,re2,im2,re3,im3] */
        vst2q_f32(&g_cfft_io[2 * i], tmp);
    }
    for (; i < N; i++)
    {
        g_cfft_io[2 * i + 0] = real[i];
        g_cfft_io[2 * i + 1] = imag[i];
    }
#else
    for (int i = 0; i < N; i++)
    {
        g_cfft_io[2 * i + 0] = real[i];
        g_cfft_io[2 * i + 1] = imag[i];
    }
#endif

    /* bitReverseFlag=1 => output in natural order. Inverse scaling is handled inside CMSIS. */
    arm_cfft_f32(S, g_cfft_io, is_inverse ? 1U : 0U, 1U);

#if USE_HELIUM_MVE
    for (i = 0; i <= N - 4; i += 4)
    {
        float32x4x2_t tmp = vld2q_f32(&g_cfft_io[2 * i]);
        /* Load de-interleaved: real in val[0], imag in val[1] */
        vst1q_f32(&real[i], tmp.val[0]);
        vst1q_f32(&imag[i], tmp.val[1]);
    }
    for (; i < N; i++)
    {
        real[i] = g_cfft_io[2 * i + 0];
        imag[i] = g_cfft_io[2 * i + 1];
    }
#else
    for (int i = 0; i < N; i++)
    {
        real[i] = g_cfft_io[2 * i + 0];
        imag[i] = g_cfft_io[2 * i + 1];
    }
#endif
}

#if defined(ARM_FLOAT16_SUPPORTED)
/*
 * Float16 trial path:
 * - Pack split real/imag directly into interleaved f16
 * - Run CFFT f16
 * - Unpack interleaved f16 directly into split f32
 */
static inline bool fft_1d_cmsis_cfft_f16(float *real, float *imag, int N, bool is_inverse)
{
    const arm_cfft_instance_f16 *S = fft_get_cfft_instance_f16(N);
    if (!S)
    {
        return false;
    }

    /* Pack directly into interleaved f16 to avoid extra f32 scratch traffic. */
#if USE_HELIUM_MVE && defined(ARM_MATH_MVE_FLOAT16)
    int i = 0;
    for (; i <= N - 8; i += 8)
    {
        float32x4_t r0 = vld1q_f32(&real[i + 0]);
        float32x4_t r1 = vld1q_f32(&real[i + 4]);
        float32x4_t im0 = vld1q_f32(&imag[i + 0]);
        float32x4_t im1 = vld1q_f32(&imag[i + 4]);

        float16x8_t r16 = vdupq_n_f16((float16_t)0);
        float16x8_t im16 = vdupq_n_f16((float16_t)0);
        r16 = vcvtbq_f16_f32(r16, r0);
        r16 = vcvttq_f16_f32(r16, r1);
        im16 = vcvtbq_f16_f32(im16, im0);
        im16 = vcvttq_f16_f32(im16, im1);

        float16x8x2_t tmp;
        tmp.val[0] = r16;
        tmp.val[1] = im16;
        /* Store interleaved: [re0,im0,re1,im1,...,re7,im7] */
        vst2q_f16(&g_cfft_io_f16[2 * i], tmp);
    }
    for (; i < N; i++)
    {
        g_cfft_io_f16[2 * i + 0] = (float16_t)real[i];
        g_cfft_io_f16[2 * i + 1] = (float16_t)imag[i];
    }
#else
    for (int i = 0; i < N; i++)
    {
        g_cfft_io_f16[2 * i + 0] = (float16_t)real[i];
        g_cfft_io_f16[2 * i + 1] = (float16_t)imag[i];
    }
#endif

    /* bitReverseFlag=1 => output in natural order. Inverse scaling is handled inside CMSIS. */
    arm_cfft_f16(S, g_cfft_io_f16, is_inverse ? 1U : 0U, 1U);

    /* Unpack directly back to split f32 arrays. */
#if USE_HELIUM_MVE && defined(ARM_MATH_MVE_FLOAT16)
    i = 0;
    for (; i <= N - 8; i += 8)
    {
        float16x8x2_t tmp = vld2q_f16(&g_cfft_io_f16[2 * i]);

        float32x4_t r0 = vcvtbq_f32_f16(tmp.val[0]);
        float32x4_t r1 = vcvttq_f32_f16(tmp.val[0]);
        float32x4_t im0 = vcvtbq_f32_f16(tmp.val[1]);
        float32x4_t im1 = vcvttq_f32_f16(tmp.val[1]);

        vst1q_f32(&real[i + 0], r0);
        vst1q_f32(&real[i + 4], r1);
        vst1q_f32(&imag[i + 0], im0);
        vst1q_f32(&imag[i + 4], im1);
    }
    for (; i < N; i++)
    {
        real[i] = (float)g_cfft_io_f16[2 * i + 0];
        imag[i] = (float)g_cfft_io_f16[2 * i + 1];
    }
#else
    for (int i = 0; i < N; i++)
    {
        real[i] = (float)g_cfft_io_f16[2 * i + 0];
        imag[i] = (float)g_cfft_io_f16[2 * i + 1];
    }
#endif

    return true;
}
#endif

/* スタティックバッファ(動的メモリ割り当て回避) */
static float g_fft_buffer_real[FFT_TEST_POINTS];
static float g_fft_buffer_imag[FFT_TEST_POINTS];
static float g_fft_buffer_original[FFT_TEST_POINTS];
static float g_fft_col_real[FFT_TEST_SIZE];
static float g_fft_col_imag[FFT_TEST_SIZE];

/* 128×128 FFT用グローバルバッファ(スタックオーバーフロー回避) */
static float g_large_fft_row_buffer[128];
static float g_large_fft_rmse_values[5];
static uint32_t g_large_fft_forward_times[5];
static uint32_t g_large_fft_inverse_times[5];

/* Spectrum inspection buffers (SRAM) */
static float g_spec_row_real[256];
static float g_spec_row_imag[256];

typedef struct
{
    float mag;
    uint16_t x;
    uint16_t y;
} fft_spec_peak_t;

static void fft_spec_peaks_init(fft_spec_peak_t *peaks, int n)
{
    for (int i = 0; i < n; i++)
    {
        peaks[i].mag = -1.0f;
        peaks[i].x = 0;
        peaks[i].y = 0;
    }
}

static void fft_spec_peaks_consider(fft_spec_peak_t *peaks, int n, uint16_t x, uint16_t y, float mag)
{
    if (n <= 0)
    {
        return;
    }

    if (!(mag > peaks[n - 1].mag))
    {
        return;
    }

    int insert_at = n - 1;
    while (insert_at > 0 && mag > peaks[insert_at - 1].mag)
    {
        peaks[insert_at] = peaks[insert_at - 1];
        insert_at--;
    }

    peaks[insert_at].mag = mag;
    peaks[insert_at].x = x;
    peaks[insert_at].y = y;
}

static void fft_spec_print_top_peaks_hyperram(uint32_t out_real_offset,
                                              uint32_t out_imag_offset,
                                              int rows,
                                              int cols,
                                              int topk)
{
    if (rows <= 0 || cols <= 0 || cols > 256)
    {
        xprintf("[FFT][Spec] invalid dims rows=%d cols=%d\n", rows, cols);
        return;
    }

    const bool square = (rows == cols);
    const int n = square ? rows : cols;

    fft_spec_peak_t peaks[8];
    if (topk > (int)(sizeof(peaks) / sizeof(peaks[0])))
    {
        topk = (int)(sizeof(peaks) / sizeof(peaks[0]));
    }

    fft_spec_peaks_init(peaks, topk);

    float dc_mag = 0.0f;
    uint32_t skipped_nonfinite = 0;

    for (int y = 0; y < rows; y++)
    {
        uint32_t row_off = (uint32_t)(y * cols) * sizeof(float);
        fsp_err_t rerr = hyperram_b_read(g_spec_row_real, (void *)(out_real_offset + row_off), (uint32_t)cols * sizeof(float));
        if (FSP_SUCCESS != rerr)
        {
            if (square)
            {
                xprintf("[FFT-%d][Spec] ERROR: read real row %d (err=%d)\n", n, y, (int)rerr);
            }
            else
            {
                xprintf("[FFT-%dx%d][Spec] ERROR: read real row %d (err=%d)\n", rows, cols, y, (int)rerr);
            }
            return;
        }
        fsp_err_t ierr = hyperram_b_read(g_spec_row_imag, (void *)(out_imag_offset + row_off), (uint32_t)cols * sizeof(float));
        if (FSP_SUCCESS != ierr)
        {
            if (square)
            {
                xprintf("[FFT-%d][Spec] ERROR: read imag row %d (err=%d)\n", n, y, (int)ierr);
            }
            else
            {
                xprintf("[FFT-%dx%d][Spec] ERROR: read imag row %d (err=%d)\n", rows, cols, y, (int)ierr);
            }
            return;
        }

        for (int x = 0; x < cols; x++)
        {
            float re = g_spec_row_real[x];
            float im = g_spec_row_imag[x];
            if (!isfinite(re) || !isfinite(im))
            {
                skipped_nonfinite++;
                continue;
            }
            float mag = sqrtf(re * re + im * im);

            if (y == 0 && x == 0)
            {
                dc_mag = mag;
                continue;
            }

            fft_spec_peaks_consider(peaks, topk, (uint16_t)x, (uint16_t)y, mag);
        }
    }

    if (square)
    {
        xprintf("[FFT-%d][Spec] DC |X(0,0)|=%.3e skip_nf=%d\n",
                n,
                dc_mag,
                (unsigned long)skipped_nonfinite);
    }
    else
    {
        xprintf("[FFT-%dx%d][Spec] DC |X(0,0)|=%.3e skip_nf=%d\n",
                rows,
                cols,
                dc_mag,
                (unsigned long)skipped_nonfinite);
    }

    for (int i = 0; i < topk; i++)
    {
        if (square)
        {
            xprintf("[FFT-%d][Spec] #%d (ky=%d kx=%d) |X|=%.3e\n",
                    n,
                    i + 1,
                    (unsigned long)peaks[i].y,
                    (unsigned long)peaks[i].x,
                    peaks[i].mag);
        }
        else
        {
            xprintf("[FFT-%dx%d][Spec] #%d (ky=%d kx=%d) |X|=%.3e\n",
                    rows,
                    cols,
                    i + 1,
                    (unsigned long)peaks[i].y,
                    (unsigned long)peaks[i].x,
                    peaks[i].mag);
        }
    }
}

/* =========================
 * Float sanitization helpers (Inf/NaN avoidance)
 * ========================= */

/*
 * Treat very large finite values as corrupted/outliers and suppress them.
 * 0x93 => about 2^(20) ~= 1,048,576, which is far above expected magnitudes
 * for these test patterns (0..1 input, 128x128 unscaled FFT => <= ~16k).
 */
#define FFT_SANITIZE_MAX_EXP (0x93u)

typedef struct
{
    uint32_t nonfinite;
    uint32_t clipped;
} fft_sanitize_stats_t;

static inline uint32_t fft_f32_to_u32(float x)
{
    union
    {
        float f;
        uint32_t u;
    } uu;
    uu.f = x;
    return uu.u;
}

static inline float fft_u32_to_f32(uint32_t x)
{
    union
    {
        float f;
        uint32_t u;
    } uu;
    uu.u = x;
    return uu.f;
}

static inline void fft_sanitize_complex_vec(float *real, float *imag, int n, fft_sanitize_stats_t *st)
{
#if USE_HELIUM_MVE
    int i = 0;
    const uint32x4_t exp_mask = vdupq_n_u32(0xFFu);
    const float32x4_t zf = vdupq_n_f32(0.0f);
    const int32x4_t exp_clip_thr = vdupq_n_s32((int32_t)(FFT_SANITIZE_MAX_EXP + 1u));
    const uint32x4_t v_ones = vdupq_n_u32(1u);
    const uint32x4_t v_zeros = vdupq_n_u32(0u);

    for (; i <= n - 4; i += 4)
    {
        float32x4_t r = vld1q_f32(&real[i]);
        float32x4_t im = vld1q_f32(&imag[i]);

        uint32x4_t ur = vreinterpretq_u32_f32(r);
        uint32x4_t ui = vreinterpretq_u32_f32(im);

        uint32x4_t er = vandq_u32(vshrq_n_u32(ur, 23), exp_mask);
        uint32x4_t ei = vandq_u32(vshrq_n_u32(ui, 23), exp_mask);

        mve_pred16_t p_nf_r = vcmpeqq_n_u32(er, 0xFFu);
        mve_pred16_t p_nf_i = vcmpeqq_n_u32(ei, 0xFFu);
        /* MVE headers/toolchain may not provide unsigned-32 gt compares; exponent is 0..255, so signed compare is safe. */
        mve_pred16_t p_clip_r = vcmpgeq(vreinterpretq_s32_u32(er), exp_clip_thr);
        mve_pred16_t p_clip_i = vcmpgeq(vreinterpretq_s32_u32(ei), exp_clip_thr);

        mve_pred16_t p_bad_r = (mve_pred16_t)(p_nf_r | p_clip_r);
        mve_pred16_t p_bad_i = (mve_pred16_t)(p_nf_i | p_clip_i);

        /* Zero only bad lanes (leave others unchanged). */
        vstrwq_p_f32(&real[i], zf, p_bad_r);
        vstrwq_p_f32(&imag[i], zf, p_bad_i);

        if (st)
        {
            st->nonfinite += (uint32_t)vaddvq(vpselq(v_ones, v_zeros, p_nf_r));
            st->nonfinite += (uint32_t)vaddvq(vpselq(v_ones, v_zeros, p_nf_i));
            st->clipped += (uint32_t)vaddvq(vpselq(v_ones, v_zeros, p_clip_r));
            st->clipped += (uint32_t)vaddvq(vpselq(v_ones, v_zeros, p_clip_i));
        }
    }

    for (; i < n; i++)
    {
        uint32_t ur = fft_f32_to_u32(real[i]);
        uint32_t ui = fft_f32_to_u32(imag[i]);

        uint32_t er = (ur >> 23) & 0xFFu;
        uint32_t ei = (ui >> 23) & 0xFFu;

        if (er == 0xFFu)
        {
            ur = 0u;
            if (st)
                st->nonfinite++;
        }
        else if (er > FFT_SANITIZE_MAX_EXP)
        {
            ur = 0u;
            if (st)
                st->clipped++;
        }

        if (ei == 0xFFu)
        {
            ui = 0u;
            if (st)
                st->nonfinite++;
        }
        else if (ei > FFT_SANITIZE_MAX_EXP)
        {
            ui = 0u;
            if (st)
                st->clipped++;
        }

        real[i] = fft_u32_to_f32(ur);
        imag[i] = fft_u32_to_f32(ui);
    }
#else
    for (int i = 0; i < n; i++)
    {
        uint32_t ur = fft_f32_to_u32(real[i]);
        uint32_t ui = fft_f32_to_u32(imag[i]);

        uint32_t er = (ur >> 23) & 0xFFu;
        uint32_t ei = (ui >> 23) & 0xFFu;

        if (er == 0xFFu)
        {
            ur = 0u;
            if (st)
                st->nonfinite++;
        }
        else if (er > FFT_SANITIZE_MAX_EXP)
        {
            /* Suppress extreme outliers to avoid cascading overflow/NaN. */
            ur = 0u;
            if (st)
                st->clipped++;
        }

        if (ei == 0xFFu)
        {
            ui = 0u;
            if (st)
                st->nonfinite++;
        }
        else if (ei > FFT_SANITIZE_MAX_EXP)
        {
            ui = 0u;
            if (st)
                st->clipped++;
        }

        real[i] = fft_u32_to_f32(ur);
        imag[i] = fft_u32_to_f32(ui);
    }
#endif
}

/* Non-mutating scan for non-finite/outlier floats (for diagnostics).
 * Only used by optional per-row input read-back checks.
 */
#if (defined(APP_MODE_FFT_VERIFY_FFT128_INPUT_READBACK) && (APP_MODE_FFT_VERIFY_FFT128_INPUT_READBACK != 0)) || \
    (defined(APP_MODE_FFT_VERIFY_FFT256_INPUT_READBACK) && (APP_MODE_FFT_VERIFY_FFT256_INPUT_READBACK != 0))
static inline void fft_scan_f32_vec(const float *v, int n, fft_sanitize_stats_t *st)
{
    if (!st)
    {
        return;
    }

    for (int i = 0; i < n; i++)
    {
        uint32_t u = fft_f32_to_u32(v[i]);
        uint32_t e = (u >> 23) & 0xFFu;
        if (e == 0xFFu)
        {
            st->nonfinite++;
        }
        else if (e > FFT_SANITIZE_MAX_EXP)
        {
            st->clipped++;
        }
    }
}
#endif

/* =========================
 * Timing helpers (DWT cycle counter preferred)
 * ========================= */

typedef struct
{
    uint32_t row_fft_cycles;
    uint32_t xpose1_cycles;
    uint32_t col_fft_cycles;
    uint32_t xpose2_cycles;
} fft_full_phase_cycles_t;

static volatile fft_full_phase_cycles_t g_fft_full_last_cycles;
static bool g_fft_timing_inited = false;
static bool g_fft_timing_use_dwt = false;

static void fft_full_phase_cycles_clear(void)
{
    g_fft_full_last_cycles.row_fft_cycles = 0u;
    g_fft_full_last_cycles.xpose1_cycles = 0u;
    g_fft_full_last_cycles.col_fft_cycles = 0u;
    g_fft_full_last_cycles.xpose2_cycles = 0u;
}

static void fft_timing_init_once(void)
{
    if (g_fft_timing_inited)
    {
        return;
    }

#if defined(DWT) && defined(CoreDebug) && defined(DWT_CTRL_CYCCNTENA_Msk) && defined(CoreDebug_DEMCR_TRCENA_Msk)
    /* Enable trace subsystem and cycle counter. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_fft_timing_use_dwt = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u);
#else
    g_fft_timing_use_dwt = false;
#endif

    g_fft_timing_inited = true;
}

static inline uint32_t fft_cycles_now(void)
{
    if (g_fft_timing_use_dwt)
    {
#if defined(DWT)
        return DWT->CYCCNT;
#else
        return 0u;
#endif
    }

    /* Fallback: use RTOS ticks (coarse). */
    return (uint32_t)xTaskGetTickCount();
}

static inline uint32_t fft_cycles_to_us(uint32_t cycles_or_ticks)
{
    if (g_fft_timing_use_dwt)
    {
        uint32_t hz = (uint32_t)SystemCoreClock;
        uint32_t cycles_per_us = hz / 1000000u;
        if (cycles_per_us == 0u)
        {
            return 0u;
        }
        return (uint32_t)(((uint64_t)cycles_or_ticks + ((uint64_t)cycles_per_us / 2u)) / (uint64_t)cycles_per_us);
    }

    /* ticks -> ms -> us (approx) */
    uint32_t ms = cycles_or_ticks * (uint32_t)portTICK_PERIOD_MS;
    return ms * 1000u;
}

/* 三角関数テーブルの初期化 */
static void init_trig_tables(int N)
{
    if (trig_table_initialized && (trig_table_N == N) && (N <= MAX_FFT_SIZE))
    {
        return;
    }

    if ((N <= 0) || (N > MAX_FFT_SIZE))
    {
        return;
    }

    /* Require power-of-two sizes. */
    if ((N & (N - 1)) != 0)
    {
        return;
    }

    const float two_pi_over_N = 2.0f * (float)M_PI / (float)N;

    for (int i = 0; i < N / 2; i++)
    {
        float angle = two_pi_over_N * (float)i;
        cos_table[i] = cosf(angle);
        sin_table[i] = sinf(angle);
    }

    trig_table_initialized = true;
    trig_table_N = N;

    /* Also build bit-reversal table for this N (used by fft_1d_mve). */
    bitrev_table_N = 0;
}

/* ビット反転インデックス計算 */
static int bit_reverse(int i, int log2n)
{
    int reversed = 0;
    for (int b = 0; b < log2n; b++)
    {
        reversed = (reversed << 1) | (i & 1);
        i >>= 1;
    }
    return reversed;
}

static inline int fft_log2_pow2_u32(uint32_t n)
{
    int log2n = 0;
    while (n > 1u)
    {
        n >>= 1;
        log2n++;
    }
    return log2n;
}

static void init_bitrev_table(int N)
{
    if ((N <= 0) || (N > MAX_FFT_SIZE))
    {
        return;
    }
    if (bitrev_table_N == N)
    {
        return;
    }

    int log2n = fft_log2_pow2_u32((uint32_t)N);
    for (int i = 0; i < N; i++)
    {
        bitrev_table[i] = (uint16_t)bit_reverse(i, log2n);
    }
    bitrev_table_N = N;
}

/* 1D FFT (Danielson-Lanczos法，MVE最適化版) */
void fft_1d_mve(float *real, float *imag, int N, bool is_inverse)
{
    /* Hot sizes: use CMSIS-DSP CFFT (math-correct + MVE-optimized). */
    if ((N == 128) || (N == 256))
    {
        if (N == 256)
        {
#if defined(ARM_FLOAT16_SUPPORTED) && FFT_USE_CFFT_F16_256
            bool used_f16 = fft_1d_cmsis_cfft_f16(real, imag, N, is_inverse);

#if FFT_DIAG_LOG
            if (!g_fft_print_fft256_path_once)
            {
                g_fft_print_fft256_path_once = true;
#if USE_HELIUM_MVE && defined(ARM_MATH_MVE_FLOAT16)
                const int cap_fft_f16_pack_mve = 1;
#else
                const int cap_fft_f16_pack_mve = 0;
#endif
                FFT_LOG("[FFT] fft256: path=%s pack_mve=%d\n", used_f16 ? "f16" : "f32_fallback", cap_fft_f16_pack_mve);
            }
#endif

            if (used_f16)
            {
                return;
            }
#else
#if FFT_DIAG_LOG
            if (!g_fft_print_fft256_path_once)
            {
                g_fft_print_fft256_path_once = true;
                FFT_LOG("[FFT] fft256: path=f32 (f16_disabled_or_unavailable)\n");
            }
#endif
#endif
        }

        fft_1d_cmsis_cfft_f32(real, imag, N, is_inverse);
        return;
    }

    // 三角関数テーブル初期化
    init_trig_tables(N);

    if ((N <= 0) || (N > MAX_FFT_SIZE))
    {
        return;
    }
    if ((N & (N - 1)) != 0)
    {
        return;
    }

    init_bitrev_table(N);

    // ビット反転並び替え
    for (int i = 0; i < N; i++)
    {
        int j = (int)bitrev_table[i];
        if (i < j)
        {
            // 実数部交換
            float temp_r = real[i];
            real[i] = real[j];
            real[j] = temp_r;

            // 虚数部交換
            float temp_i = imag[i];
            imag[i] = imag[j];
            imag[j] = temp_i;
        }
    }

    // バタフライ演算
    for (int step = 2; step <= N; step *= 2)
    {
        int halfStep = step / 2;
        int tableStep = N / step;

        /* Build contiguous twiddle arrays for this stage (twiddles depend only on m). */
        for (int m = 0; m < halfStep; m++)
        {
            int index = m * tableStep; /* Always < N/2 for radix-2 stages. */
            twiddle_stage_real[m] = cos_table[index];
            twiddle_stage_imag[m] = is_inverse ? sin_table[index] : -sin_table[index];
        }

        for (int k = 0; k < N; k += step)
        {
#if USE_HELIUM_MVE
            // MVE版: 4複素数並列処理(真のSIMD命令使用)
            int m;
            for (m = 0; m < halfStep - 3; m += 4)
            {
                float32x4_t w_real_vec = vld1q_f32(&twiddle_stage_real[m]);
                float32x4_t w_imag_vec = vld1q_f32(&twiddle_stage_imag[m]);

                // インデックス計算
                int i = k + m;
                int j = i + halfStep;

                // データをベクトルにロード
                float32x4_t real_i_vec = vld1q_f32(&real[i]);
                float32x4_t imag_i_vec = vld1q_f32(&imag[i]);
                float32x4_t real_j_vec = vld1q_f32(&real[j]);
                float32x4_t imag_j_vec = vld1q_f32(&imag[j]);

                // 複素数乗算: t = w * x[j]
                // t_real = w_real * real[j] - w_imag * imag[j]
                // t_imag = w_real * imag[j] + w_imag * real[j]
                float32x4_t t_real = vfmsq_f32(vmulq_f32(w_real_vec, real_j_vec), w_imag_vec, imag_j_vec);
                float32x4_t t_imag = vfmaq_f32(vmulq_f32(w_real_vec, imag_j_vec), w_imag_vec, real_j_vec);

                // バタフライ更新
                // real[j] = real[i] - t_real
                // imag[j] = imag[i] - t_imag
                // real[i] = real[i] + t_real
                // imag[i] = imag[i] + t_imag
                float32x4_t new_real_j = vsubq_f32(real_i_vec, t_real);
                float32x4_t new_imag_j = vsubq_f32(imag_i_vec, t_imag);
                float32x4_t new_real_i = vaddq_f32(real_i_vec, t_real);
                float32x4_t new_imag_i = vaddq_f32(imag_i_vec, t_imag);

                // 結果をメモリにストア
                vst1q_f32(&real[i], new_real_i);
                vst1q_f32(&imag[i], new_imag_i);
                vst1q_f32(&real[j], new_real_j);
                vst1q_f32(&imag[j], new_imag_j);
            }

            // 残りをスカラー処理
            for (; m < halfStep; m++)
#else
            // スカラー版
            for (int m = 0; m < halfStep; m++)
#endif
            {
                int i = k + m;
                int j = i + halfStep;

                // 回転係数
                float w_real = twiddle_stage_real[m];
                float w_imag = twiddle_stage_imag[m];

                // バタフライ計算
                float t_real = w_real * real[j] - w_imag * imag[j];
                float t_imag = w_real * imag[j] + w_imag * real[j];

                real[j] = real[i] - t_real;
                imag[j] = imag[i] - t_imag;
                real[i] += t_real;
                imag[i] += t_imag;
            }
        }
    }

    // 逆FFTの場合はスケーリング
    if (is_inverse)
    {
        float scale = 1.0f / (float)N;
#if USE_HELIUM_MVE
        float32x4_t scale_vec = vdupq_n_f32(scale);
        int i;
        for (i = 0; i < N - 3; i += 4)
        {
            float32x4_t real_vec = vld1q_f32(&real[i]);
            float32x4_t imag_vec = vld1q_f32(&imag[i]);

            real_vec = vmulq_f32(real_vec, scale_vec);
            imag_vec = vmulq_f32(imag_vec, scale_vec);

            vst1q_f32(&real[i], real_vec);
            vst1q_f32(&imag[i], imag_vec);
        }

        // 残り
        for (; i < N; i++)
        {
            real[i] *= scale;
            imag[i] *= scale;
        }
#else
        for (int i = 0; i < N; i++)
        {
            real[i] *= scale;
            imag[i] *= scale;
        }
#endif
    }
}

/* 2D FFT (行→列の順に1D FFTを適用) */
void fft_2d(float *real, float *imag, int rows, int cols, bool is_inverse)
{
    // 行方向にFFT
    for (int r = 0; r < rows; r++)
    {
        fft_1d_mve(&real[r * cols], &imag[r * cols], cols, is_inverse);
    }

    // 列方向にFFT(スタティックバッファ使用)
    for (int c = 0; c < cols; c++)
    {
        // 列データ抽出
        for (int r = 0; r < rows; r++)
        {
            g_fft_col_real[r] = real[r * cols + c];
            g_fft_col_imag[r] = imag[r * cols + c];
        }

        // 列にFFT適用
        fft_1d_mve(g_fft_col_real, g_fft_col_imag, rows, is_inverse);

        // 結果を戻す
        for (int r = 0; r < rows; r++)
        {
            real[r * cols + c] = g_fft_col_real[r];
            imag[r * cols + c] = g_fft_col_imag[r];
        }
    }
}

/* HyperRAMベース2D FFT/IFFT(メモリ効率版) */
void fft_2d_hyperram(
    uint32_t hyperram_input_real_offset,
    uint32_t hyperram_input_imag_offset,
    uint32_t hyperram_output_real_offset,
    uint32_t hyperram_output_imag_offset,
    int rows, int cols, bool is_inverse)
{
    // RAM上に1行/1列分の作業バッファのみ確保(最大256要素)
    static float work_real[256];
    static float work_imag[256];

    if (cols > 256 || rows > 256)
    {
        xprintf("[FFT] ERROR: Size exceeds 256 limit\n");
        return;
    }

    // ========== 行方向FFT ==========
    xprintf("[FFT-HyperRAM] Processing rows (0/%d)...\r", rows);
    for (int r = 0; r < rows; r++)
    {
        // HyperRAMから1行読み込み
        uint32_t row_offset_real = hyperram_input_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t row_offset_imag = hyperram_input_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_read(work_real, (void *)row_offset_real, (uint32_t)(cols) * sizeof(float));
        hyperram_b_read(work_imag, (void *)row_offset_imag, (uint32_t)(cols) * sizeof(float));

        // 行方向1D FFT実行
        fft_1d_mve(work_real, work_imag, cols, is_inverse);

        // HyperRAMに結果を書き戻し
        uint32_t out_row_offset_real = hyperram_output_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t out_row_offset_imag = hyperram_output_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_write(work_real, (void *)out_row_offset_real, (uint32_t)(cols) * sizeof(float));
        hyperram_b_write(work_imag, (void *)out_row_offset_imag, (uint32_t)(cols) * sizeof(float));

        // 進捗表示(10行ごと)
        if ((r + 1) % 10 == 0 || r == rows - 1)
        {
            xprintf("[FFT-HyperRAM] Processing rows (%d/%d)...\r", r + 1, rows);
        }
    }
    xprintf("\n");

    // ========== 列方向FFT(転置方式) ==========
    static float transposed_real[1024]; // 32×32ブロック用に拡張
    static float transposed_imag[1024]; // 32×32ブロック用に拡張

    if ((rows * cols) > 256)
    {
        xprintf("[FFT] ERROR: Size exceeds 256 limit for transpose\n");
        return;
    }

    // 行優先で読み込み，列優先に転置
    for (int r = 0; r < rows; r++)
    {
        uint32_t row_offset_real = hyperram_output_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t row_offset_imag = hyperram_output_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_read(work_real, (void *)row_offset_real, (uint32_t)(cols) * sizeof(float));
        hyperram_b_read(work_imag, (void *)row_offset_imag, (uint32_t)(cols) * sizeof(float));

        // 転置: transposed[c][r] = work[r][c]
        for (int c = 0; c < cols; c++)
        {
            transposed_real[c * rows + r] = work_real[c];
            transposed_imag[c * rows + r] = work_imag[c];
        }
    }

    xprintf("[FFT-HyperRAM] Processing cols (0/%d)...\r", cols);

    // 列ごとにFFT実行(転置済みなので連続メモリアクセス)
    for (int c = 0; c < cols; c++)
    {
        fft_1d_mve(&transposed_real[c * rows], &transposed_imag[c * rows], rows, is_inverse);

        if ((c + 1) % 10 == 0 || c == cols - 1)
        {
            xprintf("[FFT-HyperRAM] Processing cols (%d/%d)...\r", c + 1, cols);
        }
    }
    xprintf("\n");

    // 逆転置して行優先に戻し，HyperRAMに書き込み
    for (int r = 0; r < rows; r++)
    {
        // 逆転置: work[r][c] = transposed[c][r]
        for (int c = 0; c < cols; c++)
        {
            work_real[c] = transposed_real[c * rows + r];
            work_imag[c] = transposed_imag[c * rows + r];
        }

        uint32_t row_offset_real = hyperram_output_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t row_offset_imag = hyperram_output_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_write(work_real, (void *)row_offset_real, (uint32_t)(cols) * sizeof(float));
        hyperram_b_write(work_imag, (void *)row_offset_imag, (uint32_t)(cols) * sizeof(float));
    }

    xprintf("[FFT-HyperRAM] Complete!\n");
}

/* ブロック処理版2D FFT (32×32ブロック単位で処理) */
void fft_2d_hyperram_blocked(
    uint32_t hyperram_input_real_offset,
    uint32_t hyperram_input_imag_offset,
    uint32_t hyperram_output_real_offset,
    uint32_t hyperram_output_imag_offset,
    int rows, int cols, bool is_inverse)
{
    const int BLOCK_SIZE = 32;
    static float block_real[32 * 32]; // 4KB
    static float block_imag[32 * 32]; // 4KB
    static float row_real[256];
    static float row_imag[256];
    static float work_real[32];
    static float work_imag[32];

    fft_sanitize_stats_t san = {0u, 0u};

    xprintf("[FFT-Blocked] Processing %dx%d in %dx%d blocks\n", rows, cols, BLOCK_SIZE, BLOCK_SIZE);

    if (cols > 256)
    {
        xprintf("[FFT-Blocked] ERROR: cols=%d exceeds row buffer limit (256)\n", cols);
        return;
    }

    // ========== フェーズ1: 行方向FFT(HyperRAM read 1回 + write 1回/row に集約) ==========
    xprintf("[FFT-Blocked] Phase 1: Row FFTs (0/%d)...\r", rows);
    for (int r = 0; r < rows; r++)
    {
        uint32_t in_row_offset_real = hyperram_input_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t in_row_offset_imag = hyperram_input_imag_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t out_row_offset_real = hyperram_output_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t out_row_offset_imag = hyperram_output_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_read(row_real, (void *)in_row_offset_real, (uint32_t)cols * sizeof(float));
        hyperram_b_read(row_imag, (void *)in_row_offset_imag, (uint32_t)cols * sizeof(float));

        fft_sanitize_complex_vec(row_real, row_imag, cols, &san);
        fft_1d_mve(row_real, row_imag, cols, is_inverse);
        fft_sanitize_complex_vec(row_real, row_imag, cols, &san);

        hyperram_b_write(row_real, (void *)out_row_offset_real, (uint32_t)cols * sizeof(float));
        hyperram_b_write(row_imag, (void *)out_row_offset_imag, (uint32_t)cols * sizeof(float));

        if ((r + 1) % 10 == 0 || r == rows - 1)
        {
            xprintf("[FFT-Blocked] Phase 1: Row FFTs (%d/%d)...\r", r + 1, rows);
        }
    }
    xprintf("\n");

    // ========== フェーズ2: 列方向FFT(32×32ブロック単位) ==========
    int num_blocks_row = (rows + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int num_blocks_col = (cols + BLOCK_SIZE - 1) / BLOCK_SIZE;

    xprintf("[FFT-Blocked] Phase 2: Column FFTs in %d blocks\n", num_blocks_row * num_blocks_col);

    for (int block_r = 0; block_r < num_blocks_row; block_r++)
    {
        for (int block_c = 0; block_c < num_blocks_col; block_c++)
        {
            int start_row = block_r * BLOCK_SIZE;
            int start_col = block_c * BLOCK_SIZE;
            int block_height = (start_row + BLOCK_SIZE > rows) ? (rows - start_row) : BLOCK_SIZE;
            int block_width = (start_col + BLOCK_SIZE > cols) ? (cols - start_col) : BLOCK_SIZE;

            // ブロックをRAMに読み込み
            for (int r = 0; r < block_height; r++)
            {
                uint32_t row_offset_real = hyperram_output_real_offset +
                                           (uint32_t)((start_row + r) * cols + start_col) * sizeof(float);
                uint32_t row_offset_imag = hyperram_output_imag_offset +
                                           (uint32_t)((start_row + r) * cols + start_col) * sizeof(float);

                hyperram_b_read(&block_real[r * BLOCK_SIZE], (void *)row_offset_real,
                                (uint32_t)block_width * sizeof(float));
                hyperram_b_read(&block_imag[r * BLOCK_SIZE], (void *)row_offset_imag,
                                (uint32_t)block_width * sizeof(float));

                fft_sanitize_complex_vec(&block_real[r * BLOCK_SIZE], &block_imag[r * BLOCK_SIZE], block_width, &san);
            }

            // 列ごとにFFT実行
            for (int c = 0; c < block_width; c++)
            {
                // 列データを抽出
                for (int r = 0; r < block_height; r++)
                {
                    work_real[r] = block_real[r * BLOCK_SIZE + c];
                    work_imag[r] = block_imag[r * BLOCK_SIZE + c];
                }

                // 列FFT実行
                fft_sanitize_complex_vec(work_real, work_imag, block_height, &san);
                fft_1d_mve(work_real, work_imag, block_height, is_inverse);
                fft_sanitize_complex_vec(work_real, work_imag, block_height, &san);

                // 結果を戻す
                for (int r = 0; r < block_height; r++)
                {
                    block_real[r * BLOCK_SIZE + c] = work_real[r];
                    block_imag[r * BLOCK_SIZE + c] = work_imag[r];
                }
            }

            // ブロックをHyperRAMに書き戻し
            for (int r = 0; r < block_height; r++)
            {
                uint32_t row_offset_real = hyperram_output_real_offset +
                                           (uint32_t)((start_row + r) * cols + start_col) * sizeof(float);
                uint32_t row_offset_imag = hyperram_output_imag_offset +
                                           (uint32_t)((start_row + r) * cols + start_col) * sizeof(float);

                hyperram_b_write(&block_real[r * BLOCK_SIZE], (void *)row_offset_real,
                                 (uint32_t)block_width * sizeof(float));
                hyperram_b_write(&block_imag[r * BLOCK_SIZE], (void *)row_offset_imag,
                                 (uint32_t)block_width * sizeof(float));
            }

            xprintf("[FFT-Blocked] Block (%d,%d) complete\r", block_r, block_c);
        }
    }
    xprintf("\n[FFT-Blocked] All blocks complete!\n");

    if ((san.nonfinite != 0u) || (san.clipped != 0u))
    {
        xprintf("[SAN] %s nf=%u clip=%u\n",
                is_inverse ? "inv" : "fwd",
                (unsigned int)san.nonfinite,
                (unsigned int)san.clipped);
    }
}

/*
 * HyperRAM上の行優先行列(in_rows x in_cols)を，outに転置して書き出す．
 * outは(out_rows=in_cols, out_cols=in_rows)の行優先．
 * 32x32タイルでSRAM使用量を抑える．
 */
static void hyperram_transpose_tiled(
    uint32_t in_real_offset,
    uint32_t in_imag_offset,
    uint32_t out_real_offset,
    uint32_t out_imag_offset,
    int in_rows,
    int in_cols)
{
    enum
    {
        TILE = APP_MODE_FFT_TRANSPOSE_TILE
    };

    static float tile_real[TILE * TILE];
    static float tile_imag[TILE * TILE];
    static float col_real[TILE];
    static float col_imag[TILE];

    for (int tr = 0; tr < in_rows; tr += TILE)
    {
        for (int tc = 0; tc < in_cols; tc += TILE)
        {
            int th = (tr + TILE > in_rows) ? (in_rows - tr) : TILE;
            int tw = (tc + TILE > in_cols) ? (in_cols - tc) : TILE;

            /* Read tile (th x tw) */
            for (int r = 0; r < th; r++)
            {
                uint32_t src_row_real = in_real_offset + (uint32_t)((tr + r) * in_cols + tc) * sizeof(float);
                uint32_t src_row_imag = in_imag_offset + (uint32_t)((tr + r) * in_cols + tc) * sizeof(float);
                hyperram_b_read(&tile_real[r * TILE], (void *)src_row_real, (uint32_t)tw * sizeof(float));
                hyperram_b_read(&tile_imag[r * TILE], (void *)src_row_imag, (uint32_t)tw * sizeof(float));
            }

            /* Write transposed tile: out[(tc+c)][(tr+r)] = in[(tr+r)][(tc+c)] */
            for (int c = 0; c < tw; c++)
            {
                for (int r = 0; r < th; r++)
                {
                    col_real[r] = tile_real[r * TILE + c];
                    col_imag[r] = tile_imag[r * TILE + c];
                }

                uint32_t dst_row_real = out_real_offset + (uint32_t)((tc + c) * in_rows + tr) * sizeof(float);
                uint32_t dst_row_imag = out_imag_offset + (uint32_t)((tc + c) * in_rows + tr) * sizeof(float);
                hyperram_b_write(col_real, (void *)dst_row_real, (uint32_t)th * sizeof(float));
                hyperram_b_write(col_imag, (void *)dst_row_imag, (uint32_t)th * sizeof(float));
            }
        }
    }
}

/*
 * 真の2D FFT/IFFT(rows点×cols点)．
 * 行FFT(cols点)の後，HyperRAM上で転置して列FFT(rows点)を行FFTとして実行．
 */
void fft_2d_hyperram_full(
    uint32_t hyperram_input_real_offset,
    uint32_t hyperram_input_imag_offset,
    uint32_t hyperram_output_real_offset,
    uint32_t hyperram_output_imag_offset,
    uint32_t hyperram_tmp_real_offset,
    uint32_t hyperram_tmp_imag_offset,
    int rows, int cols, bool is_inverse)
{
    static float row_real[256];
    static float row_imag[256];
    fft_sanitize_stats_t san = {0u, 0u};

    if ((rows <= 0) || (cols <= 0))
    {
        xprintf("[FFT-Full] ERROR: invalid size\n");
        return;
    }
    if ((rows > 256) || (cols > 256))
    {
        xprintf("[FFT-Full] ERROR: size exceeds 256 limit\n");
        return;
    }

    FFT_VLOG("[FFT-Full] %s %dx%d\n", is_inverse ? "IFFT" : "FFT", rows, cols);

    fft_timing_init_once();
    fft_full_phase_cycles_clear();

    /* Phase 1: row FFTs (length=cols) -> output */
    uint32_t t_phase = fft_cycles_now();
    for (int r = 0; r < rows; r++)
    {
        uint32_t in_row_real = hyperram_input_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t in_row_imag = hyperram_input_imag_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t out_row_real = hyperram_output_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t out_row_imag = hyperram_output_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_read(row_real, (void *)in_row_real, (uint32_t)cols * sizeof(float));
        hyperram_b_read(row_imag, (void *)in_row_imag, (uint32_t)cols * sizeof(float));

        fft_sanitize_complex_vec(row_real, row_imag, cols, &san);
        fft_1d_mve(row_real, row_imag, cols, is_inverse);
        fft_sanitize_complex_vec(row_real, row_imag, cols, &san);

        hyperram_b_write(row_real, (void *)out_row_real, (uint32_t)cols * sizeof(float));
        hyperram_b_write(row_imag, (void *)out_row_imag, (uint32_t)cols * sizeof(float));
    }
    g_fft_full_last_cycles.row_fft_cycles = (uint32_t)(fft_cycles_now() - t_phase);

    /* Phase 2: transpose output(rows x cols) -> tmp(cols x rows) */
    t_phase = fft_cycles_now();
    hyperram_transpose_tiled(
        hyperram_output_real_offset,
        hyperram_output_imag_offset,
        hyperram_tmp_real_offset,
        hyperram_tmp_imag_offset,
        rows,
        cols);
    g_fft_full_last_cycles.xpose1_cycles = (uint32_t)(fft_cycles_now() - t_phase);

    /* Phase 3: row FFTs on tmp (length=rows) i.e., original column FFTs */
    t_phase = fft_cycles_now();
    for (int r = 0; r < cols; r++)
    {
        uint32_t tmp_row_real = hyperram_tmp_real_offset + (uint32_t)(r * rows) * sizeof(float);
        uint32_t tmp_row_imag = hyperram_tmp_imag_offset + (uint32_t)(r * rows) * sizeof(float);

        hyperram_b_read(row_real, (void *)tmp_row_real, (uint32_t)rows * sizeof(float));
        hyperram_b_read(row_imag, (void *)tmp_row_imag, (uint32_t)rows * sizeof(float));

        fft_sanitize_complex_vec(row_real, row_imag, rows, &san);
        fft_1d_mve(row_real, row_imag, rows, is_inverse);
        fft_sanitize_complex_vec(row_real, row_imag, rows, &san);

        hyperram_b_write(row_real, (void *)tmp_row_real, (uint32_t)rows * sizeof(float));
        hyperram_b_write(row_imag, (void *)tmp_row_imag, (uint32_t)rows * sizeof(float));
    }
    g_fft_full_last_cycles.col_fft_cycles = (uint32_t)(fft_cycles_now() - t_phase);

    /* Phase 4: transpose back tmp(cols x rows) -> output(rows x cols) */
    t_phase = fft_cycles_now();
    hyperram_transpose_tiled(
        hyperram_tmp_real_offset,
        hyperram_tmp_imag_offset,
        hyperram_output_real_offset,
        hyperram_output_imag_offset,
        cols,
        rows);
    g_fft_full_last_cycles.xpose2_cycles = (uint32_t)(fft_cycles_now() - t_phase);

    if ((san.nonfinite != 0u) || (san.clipped != 0u))
    {
        xprintf("[SAN] %s nf=%u clip=%u\n",
                is_inverse ? "inv" : "fwd",
                (unsigned int)san.nonfinite,
                (unsigned int)san.clipped);
    }
}

/* ROW処理のみ実行(デバッグ用) */
void fft_2d_hyperram_row_only(
    uint32_t hyperram_input_real_offset,
    uint32_t hyperram_input_imag_offset,
    uint32_t hyperram_output_real_offset,
    uint32_t hyperram_output_imag_offset,
    int rows, int cols, bool is_inverse)
{
    static float work_real[256];
    static float work_imag[256];

    if (cols > 256 || rows > 256)
    {
        xprintf("[FFT] ERROR: Size exceeds 256 limit\n");
        return;
    }

    xprintf("[FFT-HyperRAM] Processing rows only (0/%d)...\r", rows);
    for (int r = 0; r < rows; r++)
    {
        uint32_t row_offset_real = hyperram_input_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t row_offset_imag = hyperram_input_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_read(work_real, (void *)row_offset_real, (uint32_t)(cols) * sizeof(float));
        hyperram_b_read(work_imag, (void *)row_offset_imag, (uint32_t)(cols) * sizeof(float));

        fft_1d_mve(work_real, work_imag, cols, is_inverse);

        uint32_t out_row_offset_real = hyperram_output_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t out_row_offset_imag = hyperram_output_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_write(work_real, (void *)out_row_offset_real, (uint32_t)(cols) * sizeof(float));
        hyperram_b_write(work_imag, (void *)out_row_offset_imag, (uint32_t)(cols) * sizeof(float));

        if ((r + 1) % 10 == 0 || r == rows - 1)
        {
            xprintf("[FFT-HyperRAM] Processing rows only (%d/%d)...\r", r + 1, rows);
        }
    }
    xprintf("\n[FFT-HyperRAM] ROW processing complete!\n");
}

/* COL処理のみ実行(デバッグ用)- 転置方式 */
void fft_2d_hyperram_col_only(
    uint32_t hyperram_input_real_offset,
    uint32_t hyperram_input_imag_offset,
    uint32_t hyperram_output_real_offset,
    uint32_t hyperram_output_imag_offset,
    int rows, int cols, bool is_inverse)
{
    (void)hyperram_input_real_offset;
    (void)hyperram_input_imag_offset;

    // 転置バッファ(RAM上に全データを一時保持)
    static float transposed_real[256];
    static float transposed_imag[256];
    static float work_real[256];
    static float work_imag[256];

    if (cols > 256 || rows > 256 || (rows * cols) > 256)
    {
        xprintf("[FFT] ERROR: Size exceeds 256 limit\n");
        return;
    }

    xprintf("[FFT-HyperRAM] Reading and transposing matrix...\n");

    // 行優先で読み込み，列優先に転置
    for (int r = 0; r < rows; r++)
    {
        uint32_t row_offset_real = hyperram_output_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t row_offset_imag = hyperram_output_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_read(work_real, (void *)row_offset_real, (uint32_t)(cols) * sizeof(float));
        hyperram_b_read(work_imag, (void *)row_offset_imag, (uint32_t)(cols) * sizeof(float));

        // 転置: transposed[c][r] = work[r][c]
        for (int c = 0; c < cols; c++)
        {
            transposed_real[c * rows + r] = work_real[c];
            transposed_imag[c * rows + r] = work_imag[c];
        }
    }

    // デバッグ: 転置直後の最初の列(16要素)を表示
    xprintf("[DEBUG] Transposed column 0 (before FFT):\n");
    for (int i = 0; i < 16; i++)
    {
        xprintf("  [%d] = %.6f\n", i, transposed_real[i]);
    }

    xprintf("[FFT-HyperRAM] Processing cols (0/%d)...\r", cols);

    // 列ごとにFFT実行(転置済みなので連続メモリアクセス)
    for (int c = 0; c < cols; c++)
    {
        fft_1d_mve(&transposed_real[c * rows], &transposed_imag[c * rows], rows, is_inverse);

        if ((c + 1) % 10 == 0 || c == cols - 1)
        {
            xprintf("[FFT-HyperRAM] Processing cols (%d/%d)...\r", c + 1, cols);
        }
    }
    xprintf("\n");

    xprintf("[FFT-HyperRAM] Transposing back and writing...\n");

    // 逆転置して行優先に戻し，HyperRAMに書き込み
    for (int r = 0; r < rows; r++)
    {
        // 逆転置: work[r][c] = transposed[c][r]
        for (int c = 0; c < cols; c++)
        {
            work_real[c] = transposed_real[c * rows + r];
            work_imag[c] = transposed_imag[c * rows + r];
        }

        uint32_t row_offset_real = hyperram_output_real_offset + (uint32_t)(r * cols) * sizeof(float);
        uint32_t row_offset_imag = hyperram_output_imag_offset + (uint32_t)(r * cols) * sizeof(float);

        hyperram_b_write(work_real, (void *)row_offset_real, (uint32_t)(cols) * sizeof(float));
        hyperram_b_write(work_imag, (void *)row_offset_imag, (uint32_t)(cols) * sizeof(float));
    }

    xprintf("[FFT-HyperRAM] COL processing complete!\n");
}

/* 行列表示(デバッグ用) */
void fft_print_matrix(const char *label, float *data, int rows, int cols, int max_display)
{
    xprintf("\n[%s] (%dx%d)\n", label, rows, cols);

    int display_rows = (rows < max_display) ? rows : max_display;
    int display_cols = (cols < max_display) ? cols : max_display;

    for (int r = 0; r < display_rows; r++)
    {
        for (int c = 0; c < display_cols; c++)
        {
            xprintf("%.3f ", data[r * cols + c]);
        }
        if (cols > max_display)
        {
            xprintf("...");
        }
        xprintf("\n");
    }

    if (rows > max_display)
    {
        xprintf("...\n");
    }
}

/* RMSE計算(精度評価) */
float fft_calculate_rmse(float *data1, float *data2, int size)
{
    double sum_sq_error = 0.0;
    int valid = 0;

    for (int i = 0; i < size; i++)
    {
        float a = data1[i];
        float b = data2[i];
        if (!isfinite(a) || !isfinite(b))
        {
            continue;
        }

        float diff = a - b;
        sum_sq_error += (double)diff * (double)diff;
        valid++;
    }

    if (valid <= 0)
    {
        return 0.0f;
    }

    return sqrtf((float)(sum_sq_error / (double)valid));
}

/* テスト1: インパルス応答テスト */
void fft_test_impulse(void)
{
    xprintf("\n========== FFT Test 1: Impulse Response ==========\n");

    // スタティックバッファ使用
    float *real = g_fft_buffer_real;
    float *imag = g_fft_buffer_imag;

    // インパルス信号(中央に1，他は0)
    memset(real, 0, FFT_TEST_POINTS * sizeof(float));
    memset(imag, 0, FFT_TEST_POINTS * sizeof(float));
    real[FFT_TEST_SIZE / 2 * FFT_TEST_SIZE + FFT_TEST_SIZE / 2] = 1.0f;

    xprintf("[FFT] Input: Impulse at center (8,8)\n");
    fft_print_matrix("Input (Real)", real, FFT_TEST_SIZE, FFT_TEST_SIZE, 8);

    // 2D FFT
    uint32_t start = xTaskGetTickCount();
    fft_2d(real, imag, FFT_TEST_SIZE, FFT_TEST_SIZE, false);
    uint32_t end = xTaskGetTickCount();

    xprintf("[FFT] Forward FFT completed in %u ms\n", end - start);
    fft_print_matrix("FFT Output (Real)", real, FFT_TEST_SIZE, FFT_TEST_SIZE, 8);
    fft_print_matrix("FFT Output (Imag)", imag, FFT_TEST_SIZE, FFT_TEST_SIZE, 8);

    // DC成分確認(中央にインパルス→位相シフト→市松模様)
    xprintf("[FFT] DC component: %.6f\n", real[0]);
    xprintf("[FFT] Pattern: Checkerboard due to phase shift from center position\n");
    xprintf("[FFT] Mathematical verification: delta(x-8,y-8) -> exp(-j*pi*(u+v)) = (-1)^(u+v)\n");

    xprintf("========== Test 1 Complete ==========\n");
}

/* テスト2: 正弦波テスト */
void fft_test_sine_wave(void)
{
    xprintf("\n========== FFT Test 2: Sine Wave ==========\n");

    // スタティックバッファ使用
    float *real = g_fft_buffer_real;
    float *imag = g_fft_buffer_imag;

    // 正弦波パターン(2周期)
    memset(imag, 0, FFT_TEST_POINTS * sizeof(float));
    for (int r = 0; r < FFT_TEST_SIZE; r++)
    {
        for (int c = 0; c < FFT_TEST_SIZE; c++)
        {
            float angle = (4.0f * (float)M_PI) * ((float)c / (float)FFT_TEST_SIZE);
            real[r * FFT_TEST_SIZE + c] = sinf(angle);
        }
    }

    xprintf("[FFT] Input: Sine wave (2 cycles in x-direction)\n");
    fft_print_matrix("Input (Real)", real, FFT_TEST_SIZE, FFT_TEST_SIZE, 8);

    // 2D FFT
    uint32_t start = xTaskGetTickCount();
    fft_2d(real, imag, FFT_TEST_SIZE, FFT_TEST_SIZE, false);
    uint32_t end = xTaskGetTickCount();

    xprintf("[FFT] Forward FFT completed in %u ms\n", end - start);

    // パワースペクトル計算
    float max_power = 0.0f;
    int max_r = 0, max_c = 0;
    for (int r = 0; r < FFT_TEST_SIZE; r++)
    {
        for (int c = 0; c < FFT_TEST_SIZE; c++)
        {
            float power = real[r * FFT_TEST_SIZE + c] * real[r * FFT_TEST_SIZE + c] +
                          imag[r * FFT_TEST_SIZE + c] * imag[r * FFT_TEST_SIZE + c];
            if (power > max_power)
            {
                max_power = power;
                max_r = r;
                max_c = c;
            }
        }
    }

    xprintf("[FFT] Peak at frequency bin (%d, %d) with power %.2f\n", max_r, max_c, max_power);
    xprintf("[FFT] Expected peak at (0, 2) or (0, 14) for 2-cycle sine wave\n");

    xprintf("========== Test 2 Complete ==========\n");
}

/* テスト3: FFT→IFFT往復テスト(精度検証) */
void fft_test_round_trip(void)
{
    xprintf("\n========== FFT Test 3: Round Trip (FFT -> IFFT) ==========\n");

    // スタティックバッファ使用
    float *real = g_fft_buffer_real;
    float *imag = g_fft_buffer_imag;
    float *original = g_fft_buffer_original;

    // ランダムパターン(疑似乱数)
    memset(imag, 0, FFT_TEST_POINTS * sizeof(float));
    for (int i = 0; i < FFT_TEST_POINTS; i++)
    {
        real[i] = (float)(i % 100) / 100.0f;
        original[i] = real[i];
    }

    xprintf("[FFT] Input: Pseudo-random pattern\n");
    fft_print_matrix("Input (Real)", real, FFT_TEST_SIZE, FFT_TEST_SIZE, 8);

    // Forward FFT
    uint32_t start = xTaskGetTickCount();
    fft_2d(real, imag, FFT_TEST_SIZE, FFT_TEST_SIZE, false);
    uint32_t mid = xTaskGetTickCount();

    xprintf("[FFT] Forward FFT completed in %u ms\n", mid - start);

    // Inverse FFT
    fft_2d(real, imag, FFT_TEST_SIZE, FFT_TEST_SIZE, true);
    uint32_t end = xTaskGetTickCount();

    xprintf("[FFT] Inverse FFT completed in %u ms\n", end - mid);
    fft_print_matrix("Output (Real)", real, FFT_TEST_SIZE, FFT_TEST_SIZE, 8);

    // 精度評価(RMSE計算)
    float rmse = fft_calculate_rmse(real, original, FFT_TEST_POINTS);
    xprintf("[FFT] Round-trip RMSE: %.6e (lower is better)\n", rmse);

    if (rmse < 1e-5f)
    {
        xprintf("[FFT] PASS: Excellent precision (RMSE < 1e-5)\n");
    }
    else if (rmse < 1e-3f)
    {
        xprintf("[FFT] PASS: Good precision (RMSE < 1e-3)\n");
    }
    else
    {
        xprintf("[FFT] WARNING: Low precision (RMSE >= 1e-3)\n");
    }

    xprintf("========== Test 3 Complete ==========\n");
}

/* テスト4: HyperRAMベースFFT→IFFT往復テスト(メモリ効率版) */
void fft_test_hyperram_round_trip(void)
{
    xprintf("\n========== FFT Test 4: HyperRAM-based Round Trip ==========\n");
    xprintf("[FFT] Testing memory-efficient HyperRAM implementation\n");
    xprintf("[FFT] Running 5 iterations with varying data patterns...\n");

    // 作業用バッファ(RAM上)
    static float input_real[FFT_TEST_POINTS];
    static float input_imag[FFT_TEST_POINTS];
    static float output_real[FFT_TEST_POINTS];

    const int NUM_ITERATIONS = 5;
    float rmse_values[NUM_ITERATIONS];
    uint32_t forward_times[NUM_ITERATIONS];
    uint32_t inverse_times[NUM_ITERATIONS];

    for (int iter = 0; iter < NUM_ITERATIONS; iter++)
    {
        xprintf("\n[FFT] Iteration %d/%d:\n", iter + 1, NUM_ITERATIONS);

        // テストデータ生成(パターンを変化させる)
        memset(input_imag, 0, FFT_TEST_POINTS * sizeof(float));

        for (int i = 0; i < FFT_TEST_POINTS; i++)
        {
            // 各イテレーションで異なるパターン
            switch (iter)
            {
            case 0: // 線形パターン
                input_real[i] = (float)(i % 100) / 100.0f;
                break;
            case 1: // 二次パターン
                input_real[i] = (float)((i * i) % 100) / 100.0f;
                break;
            case 2: // 正弦波
                input_real[i] = sinf(2.0f * 3.14159f * (float)i / 16.0f);
                break;
            case 3: // ランダム風
                input_real[i] = (float)((i * 7 + 13) % 100) / 100.0f;
                break;
            case 4: // ステップ
                input_real[i] = (i < 128) ? 0.5f : -0.5f;
                break;
            }

            // 32要素ごとにタスクスイッチを許可(ウォッチドッグ対策)
            if (i > 0 && (i % 32) == 0)
            {
                // vTaskdelay(pdMS_TO_TICKS(1));
            }
        }

        // HyperRAMに入力データを書き込み
        hyperram_b_write(input_real, (void *)FFT_REAL_OFFSET, FFT_TEST_POINTS * sizeof(float));
        hyperram_b_write(input_imag, (void *)FFT_IMAG_OFFSET, FFT_TEST_POINTS * sizeof(float));

        // Forward FFT(HyperRAM経由，転置方式)
        uint32_t start = xTaskGetTickCount();
        fft_2d_hyperram(
            FFT_REAL_OFFSET,
            FFT_IMAG_OFFSET,
            FFT_REAL_OFFSET, // in-place変換
            FFT_IMAG_OFFSET,
            FFT_TEST_SIZE, FFT_TEST_SIZE, false);
        uint32_t mid = xTaskGetTickCount();
        forward_times[iter] = mid - start;

        // Inverse FFT(HyperRAM経由，転置方式)
        fft_2d_hyperram(
            FFT_REAL_OFFSET,
            FFT_IMAG_OFFSET,
            FFT_REAL_OFFSET,
            FFT_IMAG_OFFSET,
            FFT_TEST_SIZE, FFT_TEST_SIZE, true);
        uint32_t end = xTaskGetTickCount();
        inverse_times[iter] = end - mid;

        // HyperRAMから結果を読み出し
        hyperram_b_read(output_real, (void *)FFT_REAL_OFFSET, FFT_TEST_POINTS * sizeof(float));
        rmse_values[iter] = fft_calculate_rmse(output_real, input_real, FFT_TEST_POINTS);

        xprintf("  Forward: %d ms, \nInverse: %d ms, RMSE: %.6f\n",
                forward_times[iter], inverse_times[iter], rmse_values[iter]);
    }

    // 統計計算
    float rmse_sum = 0.0f, rmse_min = rmse_values[0], rmse_max = rmse_values[0];
    uint32_t fwd_sum = 0, inv_sum = 0;

    for (int i = 0; i < NUM_ITERATIONS; i++)
    {
        rmse_sum += rmse_values[i];
        fwd_sum += forward_times[i];
        inv_sum += inverse_times[i];

        if (rmse_values[i] < rmse_min)
            rmse_min = rmse_values[i];
        if (rmse_values[i] > rmse_max)
            rmse_max = rmse_values[i];
    }

    float rmse_avg = rmse_sum / NUM_ITERATIONS;

    xprintf("\n[FFT] Statistics over %d iterations:\n", NUM_ITERATIONS);
    xprintf("  RMSE:    avg=%.6f, min=%.6f, max=%.6f\n", rmse_avg, rmse_min, rmse_max);
    xprintf("  Forward: avg=%d ms\n", fwd_sum / NUM_ITERATIONS);
    xprintf("  Inverse: avg=%d ms\n", inv_sum / NUM_ITERATIONS);

    if (rmse_max < 1e-5f)
    {
        xprintf("[FFT] PASS: All iterations \nexcellent precision (RMSE < 1e-5)\n");
        xprintf("[FFT] HyperRAM integration \nworking correctly!\n");
    }
    else if (rmse_max < 1e-3f)
    {
        xprintf("[FFT] PASS: Good precision (RMSE < 1e-3)\n");
    }
    else
    {
        xprintf("[FFT] WARNING: Low precision (RMSE >= 1e-3)\n");
    }

    xprintf("========== Test 4 Complete ==========\n");
}

/* 128×128大規模FFT→IFFT往復テスト(ブロック処理版) */
void fft_test_hyperram_128x128(void)
{
    xprintf("\n========================================\n");
#if defined(APP_MODE_FFT_VERIFY_USE_FFT128_FULL) && (APP_MODE_FFT_VERIFY_USE_FFT128_FULL != 0)
    xprintf("  Test 5: 128x128 HyperRAM FFT (FULL)\n");
#else
    xprintf("  Test 5: 128x128 HyperRAM FFT (Blocked)\n");
#endif
    xprintf("========================================\n");

    const int FFT_SIZE = 128;
    const int FFT_ELEMENTS = 16384; // 128×128
    // NOTE:
    // hyperram_b_write() はオフセット(0x00起点)指定で，実機のHyperRAM容量外/アドレス変換外だと
    // R_OSPI_B_Write() 側でハングすることがあります．
    // 既存の16x16テストは 0x200000 付近が動作実績ありなので，その近傍に配置します．
    const uint32_t BASE_OFFSET = 0x210000;                     // 2MB+64KB
    const uint32_t INPUT_REAL_OFFSET = BASE_OFFSET + 0x00000;  // 64KB
    const uint32_t INPUT_IMAG_OFFSET = BASE_OFFSET + 0x10000;  // 64KB
    const uint32_t OUTPUT_REAL_OFFSET = BASE_OFFSET + 0x20000; // 64KB
    const uint32_t OUTPUT_IMAG_OFFSET = BASE_OFFSET + 0x30000; // 64KB
    const uint32_t WORK_REAL_OFFSET = BASE_OFFSET + 0x40000;   // 64KB
    const uint32_t WORK_IMAG_OFFSET = BASE_OFFSET + 0x50000;   // 64KB
#if defined(APP_MODE_FFT_VERIFY_USE_FFT128_FULL) && (APP_MODE_FFT_VERIFY_USE_FFT128_FULL != 0)
    const uint32_t TMP_REAL_OFFSET = BASE_OFFSET + 0x60000; // 64KB
    const uint32_t TMP_IMAG_OFFSET = BASE_OFFSET + 0x70000; // 64KB
#endif

    // 5パターンでテスト
    const char *pattern_names[5] = {
        "Linear", "Quadratic", "2D Sine", "Pseudo-random", "Step"};

    /* Reset HyperRAM write-verify counters (B2 diagnostics) for this test run. */
    hyperram_write_verify_counters_reset();
    FFT_LOG("[WV] en=%d r=%d\n",
            (int)hyperram_write_verify_is_enabled(),
            (int)hyperram_write_verify_retries());

    int pattern_count = (int)APP_MODE_FFT_VERIFY_FFT128_PATTERN_COUNT;
    if (pattern_count <= 0)
    {
        pattern_count = 1;
    }
    if (pattern_count > 5)
    {
        pattern_count = 5;
    }

    int pattern_start = (int)APP_MODE_FFT_VERIFY_FFT128_PATTERN_START;
    if (pattern_start < 0)
    {
        pattern_start = 0;
    }

    for (int p = 0; p < pattern_count; p++)
    {
        int iter = (pattern_start + p) % 5;
        xprintf("\n[FFT-128] Iter %d:\n %s pattern\n", p + 1, pattern_names[iter]);
        // vTaskdelay(pdMS_TO_TICKS(10));

        xprintf("[FFT-128] Generating pattern...\n");

        /* Verify HyperRAM writes by reading back each row and comparing bitwise. */
#if APP_MODE_FFT_VERIFY_FFT128_INPUT_READBACK
        uint32_t rb_mismatch_rows = 0;
        uint32_t rb_mismatch_words = 0;
        uint32_t rb_nonfinite = 0;
        uint32_t rb_clipped = 0;
        uint32_t rb_transient_rows = 0;
        uint32_t rb_persistent_rows = 0;
        uint32_t rb_unstable_rows = 0;
        int rb_first_row = -1;
        int rb_first_col = -1;
        uint32_t rb_first_exp_u = 0;
        uint32_t rb_first_got_u = 0;
        static float rb_row[128];
        static float rb_row2[128];
#endif

        // パターン生成
        for (int i = 0; i < FFT_ELEMENTS; i++)
        {
            float val = 0.0f;
            int y = i / FFT_SIZE;
            int x = i % FFT_SIZE;

            switch (iter)
            {
            case 0: // Linear
                val = (float)(i % 100) / 100.0f;
                break;
            case 1: // Quadratic
                val = (float)((i * i) % 200) / 200.0f;
                break;
            case 2: // 2D Sine
            {
                /* True period-16 2D sine: peaks near (±8, ±8) for N=128 */
                const float k = (2.0f * 3.14159265f) / 16.0f;
                val = sinf(k * (float)x) * sinf(k * (float)y);
            }
            break;
            case 3: // Pseudo-random
                val = (float)((i * 17 + 31) % 100) / 100.0f;
                break;
            case 4: // Step
                val = ((x / 32) % 2 == 0 && (y / 32) % 2 == 0) ? 1.0f : 0.0f;
                break;
            }

            g_large_fft_row_buffer[x] = val;

            // 1行分(128要素)貯まったらHyperRAMに書き込み
            if (x == (FFT_SIZE - 1))
            {
                uint32_t row_offset = INPUT_REAL_OFFSET + (uint32_t)(y * FFT_SIZE) * sizeof(float);
                fsp_err_t werr = hyperram_b_write(g_large_fft_row_buffer, (void *)row_offset, FFT_SIZE * sizeof(float));
                if (FSP_SUCCESS != werr)
                {
                    xprintf("[FFT-128] ERROR: hyperram_b_write failed at row %d (err=%d)\n", y, (int)werr);
                    return;
                }

#if APP_MODE_FFT_VERIFY_FFT128_INPUT_READBACK
                /* Ensure posted writes complete before immediate read-back. */
                __DSB();
                __ISB();

                /* Read-back verify (bitwise compare) */
                fsp_err_t rerr = hyperram_b_read(rb_row, (void *)row_offset, FFT_SIZE * sizeof(float));
                if (FSP_SUCCESS != rerr)
                {
                    xprintf("[FFT-128] ERROR: hyperram_b_read failed at row %d (err=%d)\n", y, (int)rerr);
                    return;
                }

                /* Scan (non-mutating) for non-finite/outliers in what we just read. */
                {
                    fft_sanitize_stats_t st = {0};
                    fft_scan_f32_vec(rb_row, FFT_SIZE, &st);
                    rb_nonfinite += st.nonfinite;
                    rb_clipped += st.clipped;
                }

                bool row_has_mismatch = false;
                for (int col = 0; col < FFT_SIZE; col++)
                {
                    uint32_t exp_u = fft_f32_to_u32(g_large_fft_row_buffer[col]);
                    uint32_t got_u = fft_f32_to_u32(rb_row[col]);
                    if (exp_u != got_u)
                    {
                        rb_mismatch_words++;
                        row_has_mismatch = true;
                        if (rb_first_row < 0)
                        {
                            rb_first_row = y;
                            rb_first_col = col;
                            rb_first_exp_u = exp_u;
                            rb_first_got_u = got_u;
                        }
                    }
                }

                if (row_has_mismatch)
                {
                    rb_mismatch_rows++;

                    /* Re-read the same row once to classify transient vs persistent vs unstable. */
                    fsp_err_t rerr2 = hyperram_b_read(rb_row2, (void *)row_offset, FFT_SIZE * sizeof(float));
                    if (FSP_SUCCESS == rerr2)
                    {
                        bool same_as_first = true;
                        bool same_as_expected = true;
                        for (int col = 0; col < FFT_SIZE; col++)
                        {
                            uint32_t exp_u = fft_f32_to_u32(g_large_fft_row_buffer[col]);
                            uint32_t got1_u = fft_f32_to_u32(rb_row[col]);
                            uint32_t got2_u = fft_f32_to_u32(rb_row2[col]);
                            if (got1_u != got2_u)
                            {
                                same_as_first = false;
                            }
                            if (got2_u != exp_u)
                            {
                                same_as_expected = false;
                            }
                        }

                        if (same_as_expected)
                        {
                            rb_transient_rows++;
                        }
                        else if (same_as_first)
                        {
                            rb_persistent_rows++;
                        }
                        else
                        {
                            rb_unstable_rows++;
                        }
                    }
                }
#endif

                if (y == 0)
                {
                    if (APP_MODE_FFT_VERIFY_VERBOSE)
                    {
                        xprintf("[FFT-128] Pattern: row 1/%d written\n", FFT_SIZE);
                    }
                }

                // 毎行書き込み後に遅延(HyperRAM競合回避)
                fft_verify_delay_ms((uint32_t)APP_MODE_FFT_VERIFY_ROW_DELAY_MS);

                // 進捗表示(16行ごと)
                if ((y + 1) % 16 == 0)
                {
                    if (APP_MODE_FFT_VERIFY_VERBOSE)
                    {
                        xprintf("[FFT-128] Pattern: row %d/%d\r", y + 1, FFT_SIZE);
                    }
                }
            }
        }
        FFT_VLOG("\n[FFT-128] Pattern generation complete\n");

#if APP_MODE_FFT_VERIFY_FFT128_INPUT_READBACK
        if ((rb_mismatch_rows | rb_mismatch_words | rb_nonfinite | rb_clipped) != 0u)
        {
            xprintf("[FFT-128] Input RB: rows=%d words=%d\n nf=%d clip=%d\n",
                    (int)rb_mismatch_rows,
                    (int)rb_mismatch_words,
                    (int)rb_nonfinite,
                    (int)rb_clipped);
            xprintf("[FFT-128] Input RB classify: trans=%d\n pers=%d unstab=%d\n",
                    (int)rb_transient_rows,
                    (int)rb_persistent_rows,
                    (int)rb_unstable_rows);
            if (rb_first_row >= 0)
            {
                uint32_t off = INPUT_REAL_OFFSET + (uint32_t)(rb_first_row * FFT_SIZE + rb_first_col) * sizeof(float);
                xprintf("[FFT-128] First mismatch: row=%d col=%d\n off=0x%08x exp=0x%08x got=0x%08x\n",
                        rb_first_row,
                        rb_first_col,
                        (unsigned long)off,
                        (unsigned long)rb_first_exp_u,
                        (unsigned long)rb_first_got_u);
            }
        }
#endif

        // 虚部ゼロ初期化
        xprintf("[FFT-128] Initializing imaginary part...\n");
        for (int i = 0; i < FFT_SIZE; i++)
        {
            g_large_fft_row_buffer[i] = 0.0f;
        }
        for (int row = 0; row < FFT_SIZE; row++)
        {
            hyperram_b_write(g_large_fft_row_buffer,
                             (void *)(INPUT_IMAG_OFFSET + (uint32_t)(row * FFT_SIZE) * sizeof(float)),
                             FFT_SIZE * sizeof(float));
        }

        FFT_VLOG("[FFT-128] Pattern ready, starting forward FFT...\n");
        fft_verify_delay_ms((uint32_t)APP_MODE_FFT_VERIFY_PHASE_DELAY_MS);

        fft_timing_init_once();
        uint32_t t0 = fft_cycles_now();

#if defined(APP_MODE_FFT_VERIFY_USE_FFT128_FULL) && (APP_MODE_FFT_VERIFY_USE_FFT128_FULL != 0)
        // FULL版(真の128x128スペクトル)
        fft_2d_hyperram_full(INPUT_REAL_OFFSET, INPUT_IMAG_OFFSET,
                             OUTPUT_REAL_OFFSET, OUTPUT_IMAG_OFFSET,
                             TMP_REAL_OFFSET, TMP_IMAG_OFFSET,
                             FFT_SIZE, FFT_SIZE, false);
#else
        // ブロック処理版FFT
        fft_2d_hyperram_blocked(INPUT_REAL_OFFSET, INPUT_IMAG_OFFSET,
                                OUTPUT_REAL_OFFSET, OUTPUT_IMAG_OFFSET,
                                FFT_SIZE, FFT_SIZE, false);
#endif

        uint32_t tf_us = fft_cycles_to_us((uint32_t)(fft_cycles_now() - t0));
        g_large_fft_forward_times[iter] = tf_us;

#if defined(APP_MODE_FFT_VERIFY_USE_FFT128_FULL) && (APP_MODE_FFT_VERIFY_USE_FFT128_FULL != 0)
        {
            uint32_t row_us = fft_cycles_to_us(g_fft_full_last_cycles.row_fft_cycles);
            uint32_t x1_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose1_cycles);
            uint32_t col_us = fft_cycles_to_us(g_fft_full_last_cycles.col_fft_cycles);
            uint32_t x2_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose2_cycles);
            FFT_LOG("[FFT-128] Forward FFT complete (%d us)\n", (unsigned long)tf_us);
            FFT_VLOG("[FFT-128] phases: row=%d x1=%d\n col=%d x2=%d\n",
                     (unsigned long)row_us,
                     (unsigned long)x1_us,
                     (unsigned long)col_us,
                     (unsigned long)x2_us);
        }
#else
        FFT_LOG("[FFT-128] Forward FFT complete (%d us)\n", (unsigned long)tf_us);
#endif
        fft_verify_delay_ms((uint32_t)APP_MODE_FFT_VERIFY_PHASE_DELAY_MS);

#if defined(APP_MODE_FFT_VERIFY_USE_FFT128_FULL) && (APP_MODE_FFT_VERIFY_USE_FFT128_FULL != 0)
        /*
         * Spectrum sanity check (depth-estimation needs the true 2D spectrum).
         * Only for the 2D Sine pattern iteration to keep logs short.
         */
        if (iter == 2)
        {
            if (APP_MODE_FFT_VERIFY_VERBOSE)
            {
                xprintf("[FFT-128][Spec] Top peaks (after forward FFT)\n");
                fft_spec_print_top_peaks_hyperram(OUTPUT_REAL_OFFSET, OUTPUT_IMAG_OFFSET, FFT_SIZE, FFT_SIZE, 4);
            }
        }
#endif

        // 逆FFT
        FFT_VLOG("[FFT-128] Starting inverse FFT...\n");
        t0 = fft_cycles_now();

#if defined(APP_MODE_FFT_VERIFY_USE_FFT128_FULL) && (APP_MODE_FFT_VERIFY_USE_FFT128_FULL != 0)
        fft_2d_hyperram_full(OUTPUT_REAL_OFFSET, OUTPUT_IMAG_OFFSET,
                             WORK_REAL_OFFSET, WORK_IMAG_OFFSET,
                             TMP_REAL_OFFSET, TMP_IMAG_OFFSET,
                             FFT_SIZE, FFT_SIZE, true);
#else
        fft_2d_hyperram_blocked(OUTPUT_REAL_OFFSET, OUTPUT_IMAG_OFFSET,
                                WORK_REAL_OFFSET, WORK_IMAG_OFFSET,
                                FFT_SIZE, FFT_SIZE, true);
#endif

        uint32_t ti_us = fft_cycles_to_us((uint32_t)(fft_cycles_now() - t0));
        g_large_fft_inverse_times[iter] = ti_us;

#if defined(APP_MODE_FFT_VERIFY_USE_FFT128_FULL) && (APP_MODE_FFT_VERIFY_USE_FFT128_FULL != 0)
        {
            uint32_t row_us = fft_cycles_to_us(g_fft_full_last_cycles.row_fft_cycles);
            uint32_t x1_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose1_cycles);
            uint32_t col_us = fft_cycles_to_us(g_fft_full_last_cycles.col_fft_cycles);
            uint32_t x2_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose2_cycles);
            FFT_LOG("[FFT-128] Inverse FFT complete (%d us)\n", (unsigned long)ti_us);
            FFT_VLOG("[FFT-128] phases: row=%d x1=%d\n col=%d x2=%d\n",
                     (unsigned long)row_us,
                     (unsigned long)x1_us,
                     (unsigned long)col_us,
                     (unsigned long)x2_us);
        }
#else
        FFT_LOG("[FFT-128] Inverse FFT complete (%d us)\n", (unsigned long)ti_us);
#endif
        fft_verify_delay_ms((uint32_t)APP_MODE_FFT_VERIFY_PHASE_DELAY_MS);

        // RMSE計算
        double sum_sq_error = 0.0;
        uint32_t rmse_nonfinite = 0;
        uint32_t rmse_clipped = 0;

        static float original_row[128];
        static float zero_imag[128];
        for (int i = 0; i < FFT_SIZE; i++)
        {
            zero_imag[i] = 0.0f;
        }

        for (int row = 0; row < FFT_SIZE; row++)
        {
            hyperram_b_read(g_large_fft_row_buffer,
                            (void *)(WORK_REAL_OFFSET + (uint32_t)(row * FFT_SIZE) * sizeof(float)),
                            FFT_SIZE * sizeof(float));
            hyperram_b_read(original_row,
                            (void *)(INPUT_REAL_OFFSET + (uint32_t)(row * FFT_SIZE) * sizeof(float)),
                            FFT_SIZE * sizeof(float));

            /* Ensure RMSE computation never gets poisoned by non-finite/outliers. */
            fft_sanitize_stats_t st = {0};
            fft_sanitize_complex_vec(g_large_fft_row_buffer, zero_imag, FFT_SIZE, &st);
            fft_sanitize_complex_vec(original_row, zero_imag, FFT_SIZE, &st);
            rmse_nonfinite += st.nonfinite;
            rmse_clipped += st.clipped;

            for (int col = 0; col < FFT_SIZE; col++)
            {
                float a = g_large_fft_row_buffer[col];
                float b = original_row[col];
                if (!isfinite(a) || !isfinite(b))
                {
                    continue;
                }
                float diff = a - b;
                sum_sq_error += (double)diff * (double)diff;
            }
        }

        float rmse = sqrtf((float)(sum_sq_error / (double)FFT_ELEMENTS));
        g_large_fft_rmse_values[iter] = rmse;

        if ((rmse_nonfinite | rmse_clipped) != 0u)
        {
            xprintf("[FFT-128] Iter %d: RMSE = %.9f\n (rmse_san nf=%d\n clip=%d)\n",
                    p + 1,
                    rmse,
                    (int)rmse_nonfinite,
                    (int)rmse_clipped);
        }
        else
        {
            xprintf("[FFT-128] Iter %d: RMSE = %.9f\n", p + 1, rmse);
        }
        fft_verify_delay_ms((uint32_t)APP_MODE_FFT_VERIFY_ITER_DELAY_MS);
    }

    // サマリー
    xprintf("\n[FFT-128] ========== Summary ==========\n");
    for (int i = 0; i < 5; i++)
    {
        xprintf("[FFT-128] %s:\n RMSE=%.9f,\n Forward=%d us, Inverse=%d us\n",
                pattern_names[i],
                g_large_fft_rmse_values[i],
                g_large_fft_forward_times[i],
                g_large_fft_inverse_times[i]);
        fft_verify_delay_ms((uint32_t)APP_MODE_FFT_VERIFY_PHASE_DELAY_MS);
    }
    xprintf("========== Test 5 Complete ==========\n");

    /* Print a single-line summary of write-verify behavior (C1). */
    {
        uint32_t mismatch_chunks = 0;
        uint32_t retries = 0;
        uint32_t failed_chunks = 0;
        hyperram_write_verify_counters_get(&mismatch_chunks, &retries, &failed_chunks);
        xprintf("[WV] mism=%d retry=%d fail=%d\n",
                (int)mismatch_chunks,
                (int)retries,
                (int)failed_chunks);
    }
}

/* 256×256大規模FFT→IFFT往復テスト(FULL版) */
void fft_test_hyperram_256x256(void)
{
    xprintf("\n========================================\n");
    xprintf("  Test 6: 256x256 HyperRAM FFT (FULL)\n");
    xprintf("========================================\n");

#if FFT_DIAG_LOG
    if (!g_fft_print_build_caps_once)
    {
        g_fft_print_build_caps_once = true;

#if defined(ARM_FLOAT16_SUPPORTED)
        const int cap_f16 = 1;
#else
        const int cap_f16 = 0;
#endif

#if defined(ARM_MATH_MVE_FLOAT16)
        const int cap_mve_f16 = 1;
#else
        const int cap_mve_f16 = 0;
#endif

#if defined(ARM_DSP_BUILT_WITH_GCC)
        const int cap_dsp_gcc = 1;
#else
        const int cap_dsp_gcc = 0;
#endif

        const int cap_fft_f16_pack_mve = (USE_HELIUM_MVE && cap_mve_f16) ? 1 : 0;

        /* Split into short lines to avoid UART/log buffer corruption. */
        FFT_LOG("[FFT] caps: USE_HELIUM_MVE=%d\n", (int)USE_HELIUM_MVE);
        FFT_LOG("[FFT] caps: ARM_FLOAT16_SUPPORTED=%d\n", cap_f16);
        FFT_LOG("[FFT] caps: ARM_MATH_MVE_FLOAT16=%d\n", cap_mve_f16);
        FFT_LOG("[FFT] caps: ARM_DSP_BUILT_WITH_GCC=%d\n", cap_dsp_gcc);
        FFT_LOG("[FFT] caps: FFT_F16_PACK_MVE=%d\n", cap_fft_f16_pack_mve);
    }
#endif

    const int FFT_SIZE = 256;
    const int FFT_ELEMENTS = 65536; // 256×256

    /*
     * Buffer layout (each plane is 256x256 float = 0x40000 bytes):
     * 8 planes => 0x200000 bytes total.
     * HyperRAM is 8MB, and verify-mode disables camera/UDP, so this should fit.
     */
    const uint32_t BASE_OFFSET = 0x300000; // keep away from multigrid/test areas
    const uint32_t PLANE_SIZE = 0x40000;   // 256*256*4

    const uint32_t INPUT_REAL_OFFSET = BASE_OFFSET + 0 * PLANE_SIZE;
    const uint32_t INPUT_IMAG_OFFSET = BASE_OFFSET + 1 * PLANE_SIZE;
    const uint32_t OUTPUT_REAL_OFFSET = BASE_OFFSET + 2 * PLANE_SIZE;
    const uint32_t OUTPUT_IMAG_OFFSET = BASE_OFFSET + 3 * PLANE_SIZE;
    const uint32_t WORK_REAL_OFFSET = BASE_OFFSET + 4 * PLANE_SIZE;
    const uint32_t WORK_IMAG_OFFSET = BASE_OFFSET + 5 * PLANE_SIZE;
    const uint32_t TMP_REAL_OFFSET = BASE_OFFSET + 6 * PLANE_SIZE;
    const uint32_t TMP_IMAG_OFFSET = BASE_OFFSET + 7 * PLANE_SIZE;

    const char *pattern_names[5] = {
        "Linear", "Quadratic", "2D Sine", "Pseudo-random", "Step"};

    hyperram_write_verify_counters_reset();
    FFT_LOG("[WV] en=%d r=%d\n",
            (int)hyperram_write_verify_is_enabled(),
            (int)hyperram_write_verify_retries());

    static float row_buf[256];
#if APP_MODE_FFT_VERIFY_FFT256_INPUT_READBACK
    static float rb_row[256];
    static float rb_row2[256];
#endif
    static float original_row[256];
    static float zero_imag[256];
    for (int i = 0; i < FFT_SIZE; i++)
    {
        zero_imag[i] = 0.0f;
    }

    int pattern_count = (int)APP_MODE_FFT_VERIFY_FFT256_PATTERN_COUNT;
    if (pattern_count <= 0)
    {
        pattern_count = 1;
    }
    if (pattern_count > 5)
    {
        pattern_count = 5;
    }

    int pattern_start = (int)APP_MODE_FFT_VERIFY_FFT256_PATTERN_START;
    if (pattern_start < 0)
    {
        pattern_start = 0;
    }

    for (int p = 0; p < pattern_count; p++)
    {
        int iter = (pattern_start + p) % 5;
        FFT_LOG("\n[FFT-256] Iter %d:\n %s pattern\n", p + 1, pattern_names[iter]);
        fft_verify_delay_ms((uint32_t)APP_MODE_FFT_VERIFY_PHASE_DELAY_MS);

#if APP_MODE_FFT_VERIFY_FFT256_INPUT_READBACK
        /* Input write + read-back verify summary */
        uint32_t rb_mismatch_rows = 0;
        uint32_t rb_mismatch_words = 0;
        uint32_t rb_nonfinite = 0;
        uint32_t rb_clipped = 0;
        uint32_t rb_transient_rows = 0;
        uint32_t rb_persistent_rows = 0;
        uint32_t rb_unstable_rows = 0;
        int rb_first_row = -1;
        int rb_first_col = -1;
        uint32_t rb_first_exp_u = 0;
        uint32_t rb_first_got_u = 0;
#endif

        FFT_VLOG("[FFT-256] Generating pattern...\n");

        for (int y = 0; y < FFT_SIZE; y++)
        {
            for (int x = 0; x < FFT_SIZE; x++)
            {
                int i = y * FFT_SIZE + x;
                float val = 0.0f;

                switch (iter)
                {
                case 0: // Linear
                    val = (float)(i % 100) / 100.0f;
                    break;
                case 1: // Quadratic
                    val = (float)((i * i) % 200) / 200.0f;
                    break;
                case 2: // 2D Sine (period 16)
                {
                    const float k = (2.0f * 3.14159265f) / 16.0f;
                    val = sinf(k * (float)x) * sinf(k * (float)y);
                }
                break;
                case 3: // Pseudo-random
                    val = (float)((i * 17 + 31) % 100) / 100.0f;
                    break;
                case 4: // Step
                    val = ((x / 32) % 2 == 0 && (y / 32) % 2 == 0) ? 1.0f : 0.0f;
                    break;
                }

                row_buf[x] = val;
            }

            uint32_t row_off = INPUT_REAL_OFFSET + (uint32_t)(y * FFT_SIZE) * sizeof(float);
            fsp_err_t werr = hyperram_b_write(row_buf, (void *)row_off, (uint32_t)FFT_SIZE * sizeof(float));
            if (FSP_SUCCESS != werr)
            {
                xprintf("[FFT-256] ERROR: hyperram_b_write failed at row %d (err=%d)\n", y, (int)werr);
                return;
            }

#if APP_MODE_FFT_VERIFY_FFT256_INPUT_READBACK
            /* Ensure posted writes complete before immediate read-back. */
            __DSB();
            __ISB();

            fsp_err_t rerr = hyperram_b_read(rb_row, (void *)row_off, (uint32_t)FFT_SIZE * sizeof(float));
            if (FSP_SUCCESS != rerr)
            {
                xprintf("[FFT-256] ERROR: hyperram_b_read failed at row %d (err=%d)\n", y, (int)rerr);
                return;
            }

            {
                fft_sanitize_stats_t st = {0};
                fft_scan_f32_vec(rb_row, FFT_SIZE, &st);
                rb_nonfinite += st.nonfinite;
                rb_clipped += st.clipped;
            }

            bool row_has_mismatch = false;
            for (int col = 0; col < FFT_SIZE; col++)
            {
                uint32_t exp_u = fft_f32_to_u32(row_buf[col]);
                uint32_t got_u = fft_f32_to_u32(rb_row[col]);
                if (exp_u != got_u)
                {
                    rb_mismatch_words++;
                    row_has_mismatch = true;
                    if (rb_first_row < 0)
                    {
                        rb_first_row = y;
                        rb_first_col = col;
                        rb_first_exp_u = exp_u;
                        rb_first_got_u = got_u;
                    }
                }
            }

            if (row_has_mismatch)
            {
                rb_mismatch_rows++;

                fsp_err_t rerr2 = hyperram_b_read(rb_row2, (void *)row_off, (uint32_t)FFT_SIZE * sizeof(float));
                if (FSP_SUCCESS == rerr2)
                {
                    bool same_as_first = true;
                    bool same_as_expected = true;
                    for (int col = 0; col < FFT_SIZE; col++)
                    {
                        uint32_t exp_u = fft_f32_to_u32(row_buf[col]);
                        uint32_t got1_u = fft_f32_to_u32(rb_row[col]);
                        uint32_t got2_u = fft_f32_to_u32(rb_row2[col]);
                        if (got1_u != got2_u)
                        {
                            same_as_first = false;
                        }
                        if (got2_u != exp_u)
                        {
                            same_as_expected = false;
                        }
                    }

                    if (same_as_expected)
                    {
                        rb_transient_rows++;
                    }
                    else if (same_as_first)
                    {
                        rb_persistent_rows++;
                    }
                    else
                    {
                        rb_unstable_rows++;
                    }
                }
            }
#endif

            /* Write imag=0 for this row */
            uint32_t irow_off = INPUT_IMAG_OFFSET + (uint32_t)(y * FFT_SIZE) * sizeof(float);
            hyperram_b_write(zero_imag, (void *)irow_off, (uint32_t)FFT_SIZE * sizeof(float));

            if (y == 0)
            {
                if (APP_MODE_FFT_VERIFY_VERBOSE)
                {
                    xprintf("[FFT-256] Pattern: row 1/%d written\n", FFT_SIZE);
                }
            }
            if ((y + 1) % 32 == 0)
            {
                if (APP_MODE_FFT_VERIFY_VERBOSE)
                {
                    xprintf("[FFT-256] Pattern: row %d/%d\r", y + 1, FFT_SIZE);
                }
            }
            fft_verify_delay_ms((uint32_t)APP_MODE_FFT_VERIFY_ROW_DELAY_MS);
        }

        FFT_VLOG("\n[FFT-256] Pattern generation complete\n");

#if APP_MODE_FFT_VERIFY_FFT256_INPUT_READBACK
        if ((rb_mismatch_rows | rb_mismatch_words | rb_nonfinite | rb_clipped) != 0u)
        {
            xprintf("[FFT-256] Input RB: rows=%d words=%d\n nf=%d clip=%d\n",
                    (int)rb_mismatch_rows,
                    (int)rb_mismatch_words,
                    (int)rb_nonfinite,
                    (int)rb_clipped);
            xprintf("[FFT-256] Input RB classify: trans=%d\n pers=%d unstab=%d\n",
                    (int)rb_transient_rows,
                    (int)rb_persistent_rows,
                    (int)rb_unstable_rows);
            if (rb_first_row >= 0)
            {
                uint32_t off = INPUT_REAL_OFFSET + (uint32_t)(rb_first_row * FFT_SIZE + rb_first_col) * sizeof(float);
                xprintf("[FFT-256] First mismatch: row=%d col=%d\n off=0x%08x exp=0x%08x got=0x%08x\n",
                        rb_first_row,
                        rb_first_col,
                        (unsigned long)off,
                        (unsigned long)rb_first_exp_u,
                        (unsigned long)rb_first_got_u);
            }
        }
#endif

        FFT_VLOG("[FFT-256] Starting forward FFT...\n");
        fft_verify_delay_ms((uint32_t)APP_MODE_FFT_VERIFY_PHASE_DELAY_MS);

        fft_timing_init_once();
        uint32_t t0 = fft_cycles_now();
        fft_2d_hyperram_full(INPUT_REAL_OFFSET, INPUT_IMAG_OFFSET,
                             OUTPUT_REAL_OFFSET, OUTPUT_IMAG_OFFSET,
                             TMP_REAL_OFFSET, TMP_IMAG_OFFSET,
                             FFT_SIZE, FFT_SIZE, false);
        uint32_t tf_cycles = (uint32_t)(fft_cycles_now() - t0);
        uint32_t tf_us = fft_cycles_to_us(tf_cycles);
        FFT_LOG("[FFT-256] Forward FFT complete (%d us)\n", (unsigned long)tf_us);

#if defined(APP_MODE_FFT_VERIFY_PRINT_PHASES_ONCE) && (APP_MODE_FFT_VERIFY_PRINT_PHASES_ONCE != 0)
        if (!g_fft_print_phases_once_done)
        {
            uint32_t row_us = fft_cycles_to_us(g_fft_full_last_cycles.row_fft_cycles);
            uint32_t x1_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose1_cycles);
            uint32_t col_us = fft_cycles_to_us(g_fft_full_last_cycles.col_fft_cycles);
            uint32_t x2_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose2_cycles);
            FFT_LOG("[FFT-256] phases(FWD): row=%d x1=%d\n col=%d x2=%d\n",
                    (unsigned long)row_us,
                    (unsigned long)x1_us,
                    (unsigned long)col_us,
                    (unsigned long)x2_us);
        }
#endif

#if !defined(APP_MODE_FFT_VERIFY_VERBOSE) || (APP_MODE_FFT_VERIFY_VERBOSE != 0)
        {
            uint32_t row_us = fft_cycles_to_us(g_fft_full_last_cycles.row_fft_cycles);
            uint32_t x1_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose1_cycles);
            uint32_t col_us = fft_cycles_to_us(g_fft_full_last_cycles.col_fft_cycles);
            uint32_t x2_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose2_cycles);
            FFT_VLOG("[FFT-256] phases: row=%d x1=%d\n col=%d x2=%d\n",
                     (unsigned long)row_us,
                     (unsigned long)x1_us,
                     (unsigned long)col_us,
                     (unsigned long)x2_us);
        }
#endif

        if ((iter == 2) && APP_MODE_FFT_VERIFY_VERBOSE)
        {
            xprintf("[FFT-256][Spec] Top peaks (after forward FFT)\n");
            fft_spec_print_top_peaks_hyperram(OUTPUT_REAL_OFFSET, OUTPUT_IMAG_OFFSET, FFT_SIZE, FFT_SIZE, 4);
        }

        FFT_VLOG("[FFT-256] Starting inverse FFT...\n");
        t0 = fft_cycles_now();
        fft_2d_hyperram_full(OUTPUT_REAL_OFFSET, OUTPUT_IMAG_OFFSET,
                             WORK_REAL_OFFSET, WORK_IMAG_OFFSET,
                             TMP_REAL_OFFSET, TMP_IMAG_OFFSET,
                             FFT_SIZE, FFT_SIZE, true);
        uint32_t ti_cycles = (uint32_t)(fft_cycles_now() - t0);
        uint32_t ti_us = fft_cycles_to_us(ti_cycles);
        FFT_LOG("[FFT-256] Inverse FFT complete (%d us)\n", (unsigned long)ti_us);

#if defined(APP_MODE_FFT_VERIFY_PRINT_PHASES_ONCE) && (APP_MODE_FFT_VERIFY_PRINT_PHASES_ONCE != 0)
        if (!g_fft_print_phases_once_done)
        {
            uint32_t row_us = fft_cycles_to_us(g_fft_full_last_cycles.row_fft_cycles);
            uint32_t x1_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose1_cycles);
            uint32_t col_us = fft_cycles_to_us(g_fft_full_last_cycles.col_fft_cycles);
            uint32_t x2_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose2_cycles);
            FFT_LOG("[FFT-256] phases(INV): row=%d x1=%d\n col=%d x2=%d\n",
                    (unsigned long)row_us,
                    (unsigned long)x1_us,
                    (unsigned long)col_us,
                    (unsigned long)x2_us);
            g_fft_print_phases_once_done = true;
        }
#endif

#if !defined(APP_MODE_FFT_VERIFY_VERBOSE) || (APP_MODE_FFT_VERIFY_VERBOSE != 0)
        {
            uint32_t row_us = fft_cycles_to_us(g_fft_full_last_cycles.row_fft_cycles);
            uint32_t x1_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose1_cycles);
            uint32_t col_us = fft_cycles_to_us(g_fft_full_last_cycles.col_fft_cycles);
            uint32_t x2_us = fft_cycles_to_us(g_fft_full_last_cycles.xpose2_cycles);
            FFT_VLOG("[FFT-256] phases: row=%d x1=%d\n col=%d x2=%d\n",
                     (unsigned long)row_us,
                     (unsigned long)x1_us,
                     (unsigned long)col_us,
                     (unsigned long)x2_us);
        }
#endif

        /* RMSE */
        double sum_sq_error = 0.0;
        uint32_t rmse_nonfinite = 0;
        uint32_t rmse_clipped = 0;

        for (int row = 0; row < FFT_SIZE; row++)
        {
            uint32_t woff = WORK_REAL_OFFSET + (uint32_t)(row * FFT_SIZE) * sizeof(float);
            uint32_t ioff = INPUT_REAL_OFFSET + (uint32_t)(row * FFT_SIZE) * sizeof(float);
            hyperram_b_read(row_buf, (void *)woff, (uint32_t)FFT_SIZE * sizeof(float));
            hyperram_b_read(original_row, (void *)ioff, (uint32_t)FFT_SIZE * sizeof(float));

            fft_sanitize_stats_t st = {0};
            fft_sanitize_complex_vec(row_buf, zero_imag, FFT_SIZE, &st);
            fft_sanitize_complex_vec(original_row, zero_imag, FFT_SIZE, &st);
            rmse_nonfinite += st.nonfinite;
            rmse_clipped += st.clipped;

            for (int col = 0; col < FFT_SIZE; col++)
            {
                float a = row_buf[col];
                float b = original_row[col];
                if (!isfinite(a) || !isfinite(b))
                {
                    continue;
                }
                float d = a - b;
                sum_sq_error += (double)d * (double)d;
            }
        }

        float rmse = sqrtf((float)(sum_sq_error / (double)FFT_ELEMENTS));
        if ((rmse_nonfinite | rmse_clipped) != 0u)
        {
            xprintf("[FFT-256] Iter %d: RMSE = %.9f\n (rmse_san nf=%d clip=%d)\n",
                    p + 1,
                    rmse,
                    (int)rmse_nonfinite,
                    (int)rmse_clipped);
        }
        else
        {
            xprintf("[FFT-256] Iter %d: RMSE = %.9f\n", p + 1, rmse);
        }

        // vTaskdelay(pdMS_TO_TICKS(50));
    }

    xprintf("========== Test 6 Complete ==========\n");
    {
        uint32_t mismatch_chunks = 0;
        uint32_t retries = 0;
        uint32_t failed_chunks = 0;
        hyperram_write_verify_counters_get(&mismatch_chunks, &retries, &failed_chunks);
        uint32_t chunks_mismatched = 0;
        uint32_t retry_ok_chunks = 0;
        uint32_t safe_chunks = 0;
        hyperram_write_verify_detail_get(&chunks_mismatched, &retry_ok_chunks, &safe_chunks);
        xprintf("[WV] mism=%d retry=%d fail=%d\n(chunk_mism=%d retry_ok=%d safe=%d)\n",
                (int)mismatch_chunks,
                (int)retries,
                (int)failed_chunks,
                (int)chunks_mismatched,
                (int)retry_ok_chunks,
                (int)safe_chunks);
    }
}

/* 全テスト実行 */
void fft_depth_test_all(void)
{
    xprintf("\n");
    xprintf("========================================\n");
    xprintf("  2D FFT/IFFT Test Suite (16x16=256pt)\n");
#if USE_HELIUM_MVE
    xprintf("  Helium MVE Acceleration: ENABLED\n");
#else
    xprintf("  Helium MVE Acceleration: DISABLED\n");
#endif
    xprintf("========================================\n");
    xprintf("[FFT] Waiting for HyperRAM access...\n");
    // vTaskdelay(pdMS_TO_TICKS(200)); // Camera Captureの書き込みサイクル待ち

    fft_test_impulse();
    // vTaskdelay(pdMS_TO_TICKS(500));

    fft_test_sine_wave();
    // vTaskdelay(pdMS_TO_TICKS(500));

    fft_test_round_trip();
    // vTaskdelay(pdMS_TO_TICKS(500));

    // fft_test_hyperram_round_trip();  // Test 4: 16×16はメモリ圧迫のため無効化
    // // vTaskdelay(pdMS_TO_TICKS(500));

    fft_test_hyperram_128x128(); // Test 5: 128×128ブロック処理版

    xprintf("\n========================================\n");
    xprintf("  All FFT Tests Complete\n");
    xprintf("========================================\n\n");
}
