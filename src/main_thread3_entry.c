#include "main_thread3.h"
#include "hyperram_integ.h"
#include "projdefs.h"
#include "putchar_ra8usb.h"
#include "fft_depth_test.h"
#include "verify_mode.h"
#include "FreeRTOS.h"
#include "task.h"
#include "video_frame_buffer.h"
#include <string.h>
#include <math.h>

/* --- Optional GNG (Growing Neural Gas) sparse 3D-ish shape in image coordinates ---
 * Uses PQ128 focus (edge - blurred-edge) as a confidence gate, and FC128 Z (float) as z.
 * Intended to provide a stable, sparse representation across frames.
 */
#ifndef FC128_GNG_ENABLE
#define FC128_GNG_ENABLE (0)
#endif

#ifndef FC128_GNG_MAX_NODES
#define FC128_GNG_MAX_NODES (32)
#endif

/* Sample stride over the 128x128 ROI. Larger = cheaper. */
#ifndef FC128_GNG_SAMPLE_STRIDE
#define FC128_GNG_SAMPLE_STRIDE (4)
#endif

/* Hard cap on how many samples are fed into GNG per frame (keeps runtime bounded). */
#ifndef FC128_GNG_MAX_UPDATES_PER_FRAME
#define FC128_GNG_MAX_UPDATES_PER_FRAME (160)
#endif

/* Do a cheap maintenance pass every N frames (error decay + isolated node cleanup). */
#ifndef FC128_GNG_MAINT_PERIOD_FRAMES
#define FC128_GNG_MAINT_PERIOD_FRAMES (1)
#endif

/* Debug: log GNG stats every N frames (0 disables). */
#ifndef FC128_GNG_LOG_PERIOD
#define FC128_GNG_LOG_PERIOD (0)
#endif

/* Only feed samples when focus >= this. */
#ifndef FC128_GNG_FOCUS_TH
#define FC128_GNG_FOCUS_TH (12)
#endif

/* Which pixels to sample for GNG:
 * 0 = sharp edges (focus high)
 * 1 = blurred edges (edge exists but focus is low vs edge strength)
 */
#ifndef FC128_GNG_GATE_MODE
#define FC128_GNG_GATE_MODE (0)
#endif

/* Require edge magnitude >= this (uses stored edge plane). Helps avoid sampling flat areas. */
#ifndef FC128_GNG_EDGE_TH
#define FC128_GNG_EDGE_TH (40)
#endif

/* For blurred-edge sampling: require edge_blur/edge_orig >= this (Q15, 0..32768).
 * edge_blur is approximated as (edge_orig - focus).
 */
#ifndef FC128_GNG_BLUR_RATIO_MIN_Q15
#define FC128_GNG_BLUR_RATIO_MIN_Q15 (27853) /* 0.85 */
#endif

/* GNG Z source:
 * 0 = FC128 reconstructed Z (Frankot-Chellappa integration output)
 * 1 = Defocus proxy from focus plane (edge - edge(blur))  [pseudo depth]
 */
#ifndef FC128_GNG_Z_SOURCE
#define FC128_GNG_Z_SOURCE (1)
#endif

/* When FC128_GNG_Z_SOURCE==1, convert focus (0..255) to a pseudo depth value.
 * If INVERT=1 then larger blur (lower focus) becomes larger Z.
 */
#ifndef FC128_GNG_FOCUS_Z_INVERT
#define FC128_GNG_FOCUS_Z_INVERT (1)
#endif

#ifndef FC128_GNG_FOCUS_Z_GAIN
#define FC128_GNG_FOCUS_Z_GAIN (1.0f)
#endif

/* Learning rates. */
#ifndef FC128_GNG_EPS_B
#define FC128_GNG_EPS_B (0.15f)
#endif

#ifndef FC128_GNG_EPS_N
#define FC128_GNG_EPS_N (0.02f)
#endif

/* Scale Z contribution in distance/update. Smaller => XY dominates => more stable topology. */
#ifndef FC128_GNG_Z_SCALE
#define FC128_GNG_Z_SCALE (0.05f)
#endif

/* Insert a new node every LAMBDA samples (across frames). */
#ifndef FC128_GNG_LAMBDA
#define FC128_GNG_LAMBDA (50)
#endif

/* Edge aging / pruning. */
#ifndef FC128_GNG_MAX_AGE
#define FC128_GNG_MAX_AGE (40)
#endif

/* Error decay factors. */
#ifndef FC128_GNG_ALPHA
#define FC128_GNG_ALPHA (0.5f)
#endif

#ifndef FC128_GNG_D
#define FC128_GNG_D (0.995f)
#endif

/* If focus sampling yields 0 updates, optionally seed from Z at fixed points.
 * Keep 0 for normal operation to avoid phantom fixed points.
 */
#ifndef FC128_GNG_ALLOW_Z_SEED_WHEN_NO_FOCUS
#define FC128_GNG_ALLOW_Z_SEED_WHEN_NO_FOCUS (0)
#endif

/* When PQ128 ROI extends beyond the frame (due to stride), out-of-frame pixels are
 * zero-padded. That can create artificial edges near the ROI top/bottom.
 * Skip such padded rows/cols when sampling for GNG.
 */
#ifndef FC128_GNG_SKIP_PADDED_PQ128_SAMPLES
#define FC128_GNG_SKIP_PADDED_PQ128_SAMPLES (1)
#endif

/* Visualization gain for GNG export mode (sparse dots). */
#ifndef FC128_EXPORT_GNG_GAIN
#define FC128_EXPORT_GNG_GAIN (1)
#endif

/* Draw neighbor edges in GNG export mode (scanline intersection, no extra buffers). */
#ifndef FC128_EXPORT_GNG_DRAW_EDGES
#define FC128_EXPORT_GNG_DRAW_EDGES (1)
#endif

/* Base brightness for edges (0..255). */
#ifndef FC128_EXPORT_GNG_EDGE_V
#define FC128_EXPORT_GNG_EDGE_V (48)
#endif

#if defined(__clang__) || defined(__GNUC__)
#define FC128_UNUSED __attribute__((unused))

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi
                                    : v;
}

static inline uint8_t max_u8(uint8_t a, uint8_t b)
{
    return (a > b) ? a : b;
}

#if FC128_GNG_ENABLE
typedef struct
{
    uint8_t used[FC128_GNG_MAX_NODES];
    float x[FC128_GNG_MAX_NODES];
    float y[FC128_GNG_MAX_NODES];
    float z[FC128_GNG_MAX_NODES];
    float err[FC128_GNG_MAX_NODES];
    uint8_t age[FC128_GNG_MAX_NODES][FC128_GNG_MAX_NODES]; /* 255 = no edge */
    uint32_t sample_count;
    uint8_t initialized;
} fc128_gng_state_t;

static fc128_gng_state_t g_fc128_gng;

static void fc128_gng_reset(fc128_gng_state_t *st)
{
    memset(st, 0, sizeof(*st));
    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        for (int j = 0; j < FC128_GNG_MAX_NODES; j++)
        {
            st->age[i][j] = 255;
        }
    }
}

static int fc128_gng_count_used(const fc128_gng_state_t *st)
{
    int n = 0;
    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        n += (st->used[i] != 0);
    }
    return n;
}

static int fc128_gng_alloc_node(fc128_gng_state_t *st)
{
    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        if (!st->used[i])
        {
            st->used[i] = 1;
            st->err[i] = 0.0f;
            for (int j = 0; j < FC128_GNG_MAX_NODES; j++)
            {
                st->age[i][j] = 255;
                st->age[j][i] = 255;
            }
            return i;
        }
    }
    return -1;
}

static void fc128_gng_remove_node(fc128_gng_state_t *st, int idx)
{
    if ((idx < 0) || (idx >= FC128_GNG_MAX_NODES) || (!st->used[idx]))
    {
        return;
    }
    st->used[idx] = 0;
    st->err[idx] = 0.0f;
    for (int j = 0; j < FC128_GNG_MAX_NODES; j++)
    {
        st->age[idx][j] = 255;
        st->age[j][idx] = 255;
    }
}

static void fc128_gng_connect(fc128_gng_state_t *st, int a, int b)
{
    if ((a < 0) || (b < 0) || (a == b))
    {
        return;
    }
    st->age[a][b] = 0;
    st->age[b][a] = 0;
}

static void fc128_gng_disconnect(fc128_gng_state_t *st, int a, int b)
{
    if ((a < 0) || (b < 0) || (a == b))
    {
        return;
    }
    st->age[a][b] = 255;
    st->age[b][a] = 255;
}

static int fc128_gng_find_two_nearest(const fc128_gng_state_t *st, float sx, float sy, float sz, int *out_s1, int *out_s2)
{
    float best1 = 1e30f;
    float best2 = 1e30f;
    int s1 = -1;
    int s2 = -1;

    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        if (!st->used[i])
        {
            continue;
        }
        const float dx = st->x[i] - sx;
        const float dy = st->y[i] - sy;
        const float dz = (st->z[i] - sz) * FC128_GNG_Z_SCALE;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best1)
        {
            best2 = best1;
            s2 = s1;
            best1 = d2;
            s1 = i;
        }
        else if (d2 < best2)
        {
            best2 = d2;
            s2 = i;
        }
    }

    *out_s1 = s1;
    *out_s2 = s2;
    return (s1 >= 0) && (s2 >= 0);
}

static int fc128_gng_find_max_error_node(const fc128_gng_state_t *st)
{
    float best = -1.0f;
    int idx = -1;
    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        if (!st->used[i])
        {
            continue;
        }
        if (st->err[i] > best)
        {
            best = st->err[i];
            idx = i;
        }
    }
    return idx;
}

static int fc128_gng_find_max_error_neighbor(const fc128_gng_state_t *st, int q)
{
    float best = -1.0f;
    int idx = -1;
    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        if (!st->used[i])
        {
            continue;
        }
        if (st->age[q][i] == 255)
        {
            continue;
        }
        if (st->err[i] > best)
        {
            best = st->err[i];
            idx = i;
        }
    }
    return idx;
}

static void fc128_gng_prune(fc128_gng_state_t *st)
{
    /* Remove isolated nodes. (Edge aging / removal is handled locally in the update step.) */
    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        if (!st->used[i])
        {
            continue;
        }
        int deg = 0;
        for (int j = 0; j < FC128_GNG_MAX_NODES; j++)
        {
            deg += (st->age[i][j] != 255);
        }
        if (deg == 0)
        {
            fc128_gng_remove_node(st, i);
        }
    }
}

static void fc128_gng_step(fc128_gng_state_t *st, float sx, float sy, float sz)
{
    if (!st->initialized)
    {
        fc128_gng_reset(st);
        st->initialized = 1;
    }

    /* Bootstrap: need at least 2 nodes. */
    if (fc128_gng_count_used(st) < 2)
    {
        int n = fc128_gng_alloc_node(st);
        if (n >= 0)
        {
            st->x[n] = sx;
            st->y[n] = sy;
            st->z[n] = sz;
        }
        return;
    }

    int s1 = -1;
    int s2 = -1;
    if (!fc128_gng_find_two_nearest(st, sx, sy, sz, &s1, &s2))
    {
        return;
    }

    /* Age edges from s1. */
    for (int j = 0; j < FC128_GNG_MAX_NODES; j++)
    {
        if (st->age[s1][j] != 255)
        {
            if (st->age[s1][j] < 254)
            {
                st->age[s1][j]++;
                st->age[j][s1] = st->age[s1][j];
            }

            if (st->age[s1][j] > FC128_GNG_MAX_AGE)
            {
                fc128_gng_disconnect(st, s1, j);
            }
        }
    }

    /* Update winner error and move winner/its neighbors. */
    {
        const float dx = sx - st->x[s1];
        const float dy = sy - st->y[s1];
        const float dz = (sz - st->z[s1]) * FC128_GNG_Z_SCALE;
        const float d2 = dx * dx + dy * dy + dz * dz;
        st->err[s1] += d2;
        st->x[s1] += FC128_GNG_EPS_B * dx;
        st->y[s1] += FC128_GNG_EPS_B * dy;
        st->z[s1] += FC128_GNG_EPS_B * dz;
    }
    for (int j = 0; j < FC128_GNG_MAX_NODES; j++)
    {
        if (!st->used[j])
        {
            continue;
        }
        if (st->age[s1][j] == 255)
        {
            continue;
        }
        const float dx = sx - st->x[j];
        const float dy = sy - st->y[j];
        const float dz = (sz - st->z[j]) * FC128_GNG_Z_SCALE;
        st->x[j] += FC128_GNG_EPS_N * dx;
        st->y[j] += FC128_GNG_EPS_N * dy;
        st->z[j] += FC128_GNG_EPS_N * dz;
    }

    /* Connect s1-s2 (age=0). */
    fc128_gng_connect(st, s1, s2);

    /* Insert nodes periodically. */
    st->sample_count++;
    if ((FC128_GNG_LAMBDA > 0) && ((st->sample_count % FC128_GNG_LAMBDA) == 0))
    {
        int q = fc128_gng_find_max_error_node(st);
        if (q >= 0)
        {
            int f = fc128_gng_find_max_error_neighbor(st, q);
            if (f >= 0)
            {
                int r = fc128_gng_alloc_node(st);
                if (r >= 0)
                {
                    st->x[r] = 0.5f * (st->x[q] + st->x[f]);
                    st->y[r] = 0.5f * (st->y[q] + st->y[f]);
                    st->z[r] = 0.5f * (st->z[q] + st->z[f]);
                    st->err[r] = st->err[q];
                    st->err[q] *= FC128_GNG_ALPHA;
                    st->err[f] *= FC128_GNG_ALPHA;
                    fc128_gng_disconnect(st, q, f);
                    fc128_gng_connect(st, q, r);
                    fc128_gng_connect(st, r, f);
                }
            }
        }
    }

    /* Error decay is handled by per-frame maintenance (cheaper). */
}

static void fc128_gng_maint(fc128_gng_state_t *st)
{
    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        if (st->used[i])
        {
            st->err[i] *= FC128_GNG_D;
        }
    }
    fc128_gng_prune(st);
}

static void fc128_gng_render_u8_128x128(const fc128_gng_state_t *st, uint8_t *dst128)
{
    memset(dst128, 0, 128 * 128);

    float zmin = 1e30f;
    float zmax = -1e30f;
    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        if (!st->used[i])
        {
            continue;
        }
        if (st->z[i] < zmin)
        {
            zmin = st->z[i];
        }
        if (st->z[i] > zmax)
        {
            zmax = st->z[i];
        }
    }
    const float denom = (zmax > zmin) ? (zmax - zmin) : 1.0f;

    for (int i = 0; i < FC128_GNG_MAX_NODES; i++)
    {
        if (!st->used[i])
        {
            continue;
        }
        const int xi = clampi((int)(st->x[i] + 0.5f), 0, 127);
        const int yi = clampi((int)(st->y[i] + 0.5f), 0, 127);
        float t = (st->z[i] - zmin) / denom;
        int v = (int)(t * 255.0f);
        v = clampi(v * FC128_EXPORT_GNG_GAIN, 0, 255);
        dst128[yi * 128 + xi] = (uint8_t)v;
    }
}

/* Defined later (needs PQ128/FC128 layout macros). */
static void fc128_gng_update_from_frame(uint32_t frame_base_offset, uint32_t frame_seq);
#endif /* FC128_GNG_ENABLE */
#else
#define FC128_UNUSED
#endif
#include <stdint.h>
#include <float.h>

// ========== 深度復元アルゴリズム切り替え ==========
// 1 = マルチグリッド版（ポアソン方程式反復解法、中品質、中速: ~0.5-2秒/フレーム）
// 0 = 簡易版（行方向積分、低品質、高速: <1ms/フレーム）
#define USE_DEPTH_METHOD 1

// HyperRAMから直接p勾配をストリーミングして行積分する簡易版。
// USE_SIMPLE_DIRECT_P=1で有効化。
// USE_SIMPLE_DIRECT_P=0で従来のSRAMバッファ経由
#define USE_SIMPLE_DIRECT_P 1

// 輝度正規化（明るさに依存しない深度推定）
// 1 = 各行ごとにコントラストを正規化（薄暗い/明るい環境でも同じ深度）
// 0 = 正規化なし（従来動作）
#define USE_BRIGHTNESS_NORMALIZATION 1
// =================================================

// Helium MVE (ARM M-profile Vector Extension) support
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE > 0)
#include <arm_mve.h>
#define USE_HELIUM_MVE 1           // シンプルで安全なMVE実装
#define USE_MVE_FOR_MG_RESTRICT 1  // restrict/prolongを有効化
#define USE_MVE_FOR_GAUSS_SEIDEL 1 // Gauss-Seidelを有効化
#else
#define USE_HELIUM_MVE 0
#define USE_MVE_FOR_MG_RESTRICT 0
#define USE_MVE_FOR_GAUSS_SEIDEL 0
#endif

// ---- Optional FC128 per-phase cycle timing (DWT->CYCCNT) ----
#ifndef FC128_TIMING_ENABLE
#define FC128_TIMING_ENABLE (0)
#endif

/* Log every N frames. Keep this >= 10 to avoid impacting performance. */
#ifndef FC128_TIMING_LOG_PERIOD
#define FC128_TIMING_LOG_PERIOD (30U)
#endif

static inline FC128_UNUSED void fc128_dwt_init_once(void)
{
#if FC128_TIMING_ENABLE
    static bool s_inited = false;
    if (s_inited)
    {
        return;
    }
    s_inited = true;

    /* Enable trace and the cycle counter. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
}

static inline FC128_UNUSED uint32_t fc128_dwt_now(void)
{
#if FC128_TIMING_ENABLE
    return DWT->CYCCNT;
#else
    return 0U;
#endif
}

#if FC128_TIMING_ENABLE
extern uint32_t SystemCoreClock;

static inline uint32_t fc128_cyc_to_us(uint32_t cyc)
{
    uint32_t hz = SystemCoreClock;
    if (hz == 0U)
    {
        return 0U;
    }
    return (uint32_t)(((uint64_t)cyc * 1000000ULL) / (uint64_t)hz);
}
#endif

#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 2) // YUV422 = 2 bytes/pixel
#define GRADIENT_OFFSET FRAME_SIZE                  // p,q勾配マップを配置（2チャンネル×8bit）
#define DEPTH_OFFSET (FRAME_SIZE * 2)               // 深度マップ（8bit grayscale: 320×240 = 76,800バイト）

#define DEPTH_BYTES ((uint32_t)(FRAME_WIDTH * FRAME_HEIGHT))

/* Align scratch to 16 bytes (matches base alignment granularity). */
#define ALIGN16_U32(x) (((uint32_t)(x) + 15U) & ~15U)

/* ---- Tuning profile ----
 * 1 (default): classic FC behavior (minimal PQ128 pre-processing; matches prior look)
 * 0: stabilized/experimental (focus/chroma masks, tone handling, etc.)
 *
 * You can override this from build flags if needed.
 */
#ifndef FC128_PROFILE_CLASSIC
#define FC128_PROFILE_CLASSIC (1)
#endif

/* ---- Depth export (FC128 -> 320x240 u8) tunables ----
 * By default the exporter normalizes per-frame (z_min..z_max) to [0..255], which can
 * make even small variations look like strong "relative depth".
 * Enable the options below to suppress that effect.
 */
#ifndef FC128_EXPORT_FIXED_SCALE
/* 1: Fixed scale (recommended for "distance-like" heatmap)
 *    u8 is derived from a fixed Z range (plus optional z_ref).
 * 0: Per-frame min/max normalization.
 */
#define FC128_EXPORT_FIXED_SCALE (0)
#endif

#ifndef FC128_EXPORT_USE_ZREF_EMA
/* Track a slowly-varying Z offset and subtract it before fixed-range mapping.
 * Helps stabilize the visualization against DC drift.
 */
#define FC128_EXPORT_USE_ZREF_EMA (1)
#endif

#ifndef FC128_EXPORT_ZREF_EMA_SHIFT
/* EMA step for z_ref: new = old + (cur-old)/2^SHIFT. Larger = smoother. */
#define FC128_EXPORT_ZREF_EMA_SHIFT (4)
#endif

#ifndef FC128_EXPORT_Z_NEAR
/* Z value (after subtracting z_ref) that should appear RED (u8=255). */
#define FC128_EXPORT_Z_NEAR (0.50f)
#endif

#ifndef FC128_EXPORT_Z_FAR
/* Z value (after subtracting z_ref) that should appear BLUE (u8=0). */
#define FC128_EXPORT_Z_FAR (-0.50f)
#endif

#ifndef FC128_EXPORT_FIXED_AUTOCALIB
/* 1: During the first N frames, auto-expand the fixed Z range based on observed
 * (z - z_ref) min/max so the output doesn't saturate at 0/255.
 */
#define FC128_EXPORT_FIXED_AUTOCALIB (1)
#endif

#ifndef FC128_EXPORT_FIXED_AUTOCALIB_FRAMES
#define FC128_EXPORT_FIXED_AUTOCALIB_FRAMES (30U)
#endif

#ifndef FC128_EXPORT_FIXED_AUTOCALIB_MARGIN
/* Extra margin added to observed span when auto-calibrating. */
#define FC128_EXPORT_FIXED_AUTOCALIB_MARGIN (0.15f)
#endif

#ifndef FC128_EXPORT_BRIGHTNESS_BIAS_U8
/* Additive bias applied to exported u8 after mapping (and after INVERT).
 * Positive values make the heatmap brighter.
 */
#if FC128_EXPORT_FIXED_SCALE
#define FC128_EXPORT_BRIGHTNESS_BIAS_U8 (24)
#else
#define FC128_EXPORT_BRIGHTNESS_BIAS_U8 (0)
#endif
#endif

#ifndef FC128_EXPORT_USE_ZMINMAX_EMA
#define FC128_EXPORT_USE_ZMINMAX_EMA (1)
#endif

/* EMA step: new = old + (cur-old)/2^SHIFT. Smaller = smoother. */
#ifndef FC128_EXPORT_ZMINMAX_EMA_SHIFT
#define FC128_EXPORT_ZMINMAX_EMA_SHIFT (3)
#endif

/* Floor for normalization range. Larger values reduce contrast when the scene is flat.
 * Units are the same as reconstructed Z.
 */
#ifndef FC128_EXPORT_RANGE_FLOOR
#define FC128_EXPORT_RANGE_FLOOR (1.0e-6f)
#endif

/* Contrast around mid-gray (Q15). 32768=unchanged, 16384=half contrast. */
#ifndef FC128_EXPORT_CONTRAST_Q15
#if FC128_PROFILE_CLASSIC
#define FC128_EXPORT_CONTRAST_Q15 (32768)
#else
#define FC128_EXPORT_CONTRAST_Q15 (24000)
#endif
#endif

/* Invert exported depth polarity (u8): out <- 255 - out.
 * Useful when near/far appears swapped in the visualization.
 */
#ifndef FC128_EXPORT_INVERT
#define FC128_EXPORT_INVERT (0)
#endif

/* Background fill value for the exported 320x240 depth image. */
#ifndef FC128_EXPORT_BG_U8
#if FC128_PROFILE_CLASSIC
#define FC128_EXPORT_BG_U8 (0)
#else
#define FC128_EXPORT_BG_U8 (128)
#endif
#endif

/* Export selector:
 * 0 = FC128 reconstructed depth (Frankot–Chellappa Z)
 * Other debug sources were removed to reduce complexity.
 */
#ifndef FC128_EXPORT_SOURCE
#define FC128_EXPORT_SOURCE (0)
#endif

#if (FC128_EXPORT_SOURCE != 0)
#error "Only FC128_EXPORT_SOURCE=0 is supported"
#endif

#define FC128_EXPORT_SRC_DEPTH (0)

/* Extra gain when exporting focus map (0..255). */
#ifndef FC128_EXPORT_FOCUS_GAIN
#define FC128_EXPORT_FOCUS_GAIN (8)
#endif

/* Extra gain when exporting edge magnitude map (0..255). */
#ifndef FC128_EXPORT_EDGE_GAIN
#define FC128_EXPORT_EDGE_GAIN (2)
#endif

/* Extra gain when exporting blurness ratio (0..255). */
#ifndef FC128_EXPORT_BLUR_RATIO_GAIN
#define FC128_EXPORT_BLUR_RATIO_GAIN (1)
#endif

/* Suppress ratio where edge is too weak (avoids noisy division). */
#ifndef FC128_EXPORT_BLUR_EDGE_TH
#define FC128_EXPORT_BLUR_EDGE_TH (32)
#endif

// ---- 128x128 p,q (int16) generation ----
#define PQ128_SIZE (128)

/* Expand ROI coverage by subsampling (pixel skipping) while keeping PQ128_SIZE fixed.
 * Example: PQ128_SAMPLE_STRIDE_X=2, PQ128_SAMPLE_STRIDE_Y=1 samples a 256x128 region.
 */
#ifndef PQ128_SAMPLE_STRIDE_X
#define PQ128_SAMPLE_STRIDE_X (2)
#endif

#ifndef PQ128_SAMPLE_STRIDE_Y
#define PQ128_SAMPLE_STRIDE_Y (1)
#endif

#if (PQ128_SAMPLE_STRIDE_X < 1) || (PQ128_SAMPLE_STRIDE_Y < 1)
#error "PQ128_SAMPLE_STRIDE_X/Y must be >= 1"
#endif

#define PQ128_SRC_W (PQ128_SIZE * PQ128_SAMPLE_STRIDE_X)
#define PQ128_SRC_H (PQ128_SIZE * PQ128_SAMPLE_STRIDE_Y)

/* Some toolchains treat #warning as -Werror. Keep disabled by default. */
#ifndef PQ128_BUILD_WARNINGS
#define PQ128_BUILD_WARNINGS (0)
#endif

/* Width may exceed the frame; left/right will be zero-padded. */
#if (PQ128_BUILD_WARNINGS && (PQ128_SRC_W > FRAME_WIDTH))
#warning "PQ128_SRC_W exceeds frame width; left/right will be zero-padded"
#endif

/* Height may exceed the frame; out-of-frame rows are zero-padded. */
#if (PQ128_BUILD_WARNINGS && (PQ128_SRC_H > FRAME_HEIGHT))
#warning "PQ128_SRC_H exceeds frame height; top/bottom will be zero-padded"
#endif

#define PQ128_X0 ((FRAME_WIDTH - PQ128_SRC_W) / 2)
#define PQ128_Y0 ((FRAME_HEIGHT - PQ128_SRC_H) / 2)
#define PQ128_PLANE_BYTES ((uint32_t)(PQ128_SIZE * PQ128_SIZE * (int)sizeof(int16_t)))

/* Force p/q to 0 in an outer border of N pixels.
 * N=1 zeros {0th, last} row/col (good for FFT periodic boundary artifacts).
 */
#ifndef PQ128_BORDER_ZERO_N
#define PQ128_BORDER_ZERO_N (0)
#endif

#if (PQ128_BORDER_ZERO_N < 0)
#error "PQ128_BORDER_ZERO_N must be >= 0"
#endif

#if (PQ128_BORDER_ZERO_N > (PQ128_SIZE / 2))
#warning "PQ128_BORDER_ZERO_N is large; most/all ROI may become zero"
#endif

/* Compensate gradient magnitude for stride (recommended).
 * With stride>1, differences span a larger distance; dividing by stride keeps p/q
 * closer to the stride==1 tuning.
 */
#ifndef PQ128_APPLY_STRIDE_COMPENSATION
#define PQ128_APPLY_STRIDE_COMPENSATION (1)
#endif
/* Store into the region immediately after the frame. This stays within the same fixed slot
 * because VIDEO_FRAME_BASE_OFFSET_STEP defaults to 0.
 */
#define PQ128_P_OFFSET (GRADIENT_OFFSET)
#define PQ128_Q_OFFSET (PQ128_P_OFFSET + PQ128_PLANE_BYTES)

/* Optional: store a u8 focus map (edge(original)-edge(blur)) for debug/visualization. */
#ifndef PQ128_STORE_FOCUS_PLANE
#define PQ128_STORE_FOCUS_PLANE (1)
#endif

/* Blur strength for defocus cue (focus/edge_blur computation).
 * Larger values make edge_blur weaker for sharp textures/edges, improving separation.
 */
#ifndef PQ128_FOCUS_BLUR_PASSES
#if FC128_PROFILE_CLASSIC
#define PQ128_FOCUS_BLUR_PASSES (1)
#else
#define PQ128_FOCUS_BLUR_PASSES (2)
#endif
#endif

#if (PQ128_FOCUS_BLUR_PASSES < 1)
#error "PQ128_FOCUS_BLUR_PASSES must be >= 1"
#endif

#define PQ128_FOCUS_PLANE_BYTES ((uint32_t)(PQ128_SIZE * PQ128_SIZE))
#define PQ128_FOCUS_OFFSET (PQ128_Q_OFFSET + PQ128_PLANE_BYTES)

/* Optional: store edge magnitude of original image (sobel on unblurred Y) for gating/analysis. */
#ifndef PQ128_STORE_EDGE_PLANE
#define PQ128_STORE_EDGE_PLANE (1)
#endif

/* Optional: store edge magnitude of blurred image (sobel on blurred Y).
 * This enables a more direct "blurness" estimate (edge_blur/edge_orig).
 */
#ifndef PQ128_STORE_EDGE_BLUR_PLANE
#define PQ128_STORE_EDGE_BLUR_PLANE (1)
#endif

/* Optional: store a mask to suppress highlights/dark noise (0=invalid, 255=valid).
 * Used by blur_ratio export and optionally by GNG sampling.
 */
#ifndef PQ128_STORE_SATMASK_PLANE
#define PQ128_STORE_SATMASK_PLANE (1)
#endif

/* Mark invalid if any neighbor/center is >= HI_TH (specular/saturation). */
#ifndef PQ128_SATMASK_HI_TH
#define PQ128_SATMASK_HI_TH (250)
#endif

/* Mark invalid if any neighbor/center is <= LO_TH (dark noise / low SNR). */
#ifndef PQ128_SATMASK_LO_TH
#define PQ128_SATMASK_LO_TH (2)
#endif

#define PQ128_EDGE_PLANE_BYTES ((uint32_t)(PQ128_SIZE * PQ128_SIZE))
#define PQ128_EDGE_OFFSET (PQ128_FOCUS_OFFSET + PQ128_FOCUS_PLANE_BYTES)

#define PQ128_EDGE_BLUR_PLANE_BYTES ((uint32_t)(PQ128_SIZE * PQ128_SIZE))
#define PQ128_EDGE_BLUR_OFFSET (PQ128_EDGE_OFFSET + PQ128_EDGE_PLANE_BYTES)

#define PQ128_SATMASK_PLANE_BYTES ((uint32_t)(PQ128_SIZE * PQ128_SIZE))
#define PQ128_SATMASK_OFFSET (PQ128_EDGE_BLUR_OFFSET + PQ128_EDGE_BLUR_PLANE_BYTES)

/* PQ128 normalized-gradient parameters (compile-time tunables). */
#ifndef PQ128_NORM_EPS
#define PQ128_NORM_EPS (16)
#endif

#ifndef PQ128_NORM_SCALE
#define PQ128_NORM_SCALE (256)
#endif

/* p,q computation mode (compile-time):
 * 0 = normalized (default): v = (I1-I0) / (I1+I0+eps)  (uses LUT or division)
 * 1 = strong approx (no normalization): v = (I1-I0) * PQ128_DIFF_SCALE
 * 2 = strong approx (pow2 reciprocal): v = (I1-I0) * PQ128_NORM_SCALE / 2^floor(log2(den))
 * 3 = strong approx (light-model, like fcmethod/img_test.c "--pq=light"):
 *     p,q are overwritten from intensity and current light direction:
 *       p ~= (I/255) * (ps/ts), q ~= (I/255) * (qs/ts)
 */
#ifndef PQ128_PQ_MODE
#define PQ128_PQ_MODE (2)
#endif

/* Flip p/q sign if the reconstructed surface appears inverted.
 * - Flip both (P=1,Q=1) for near/far inversion.
 * - Flip only P for left-right inversion.
 * - Flip only Q for up-down inversion.
 */
#ifndef PQ128_FLIP_P
#define PQ128_FLIP_P (0)
#endif

#ifndef PQ128_FLIP_Q
#define PQ128_FLIP_Q (0)
#endif

/* Scale for PQ128_PQ_MODE==1 (unnormalized central difference). */
#ifndef PQ128_DIFF_SCALE
#define PQ128_DIFF_SCALE (1)
#endif

/* Scale applied to light-model p/q before storing to int16 (PQ128_PQ_MODE==3). */
#ifndef PQ128_LIGHTMODEL_SCALE
#define PQ128_LIGHTMODEL_SCALE (2048)
#endif

/* Forward declarations for light-model PQ mode (globals are defined later). */
#if (PQ128_PQ_MODE == 3)
static float g_light_ps;
static float g_light_qs;
static float g_light_ts;
#endif

#ifndef PQ128_SAT_TH
#define PQ128_SAT_TH (245)
#endif

/* Remove per-pixel division by using a reciprocal LUT (recommended for speed). */
#ifndef PQ128_USE_RECIP_LUT
/* NOTE:
 * PQ128_PQ_MODE==0 with PQ128_USE_RECIP_LUT==1 allocates a ~2KB uint32 LUT in .bss,
 * which can overflow internal RAM depending on thread stack sizing.
 * Default to 0 for robustness; enable explicitly if you have RAM headroom.
 */
#define PQ128_USE_RECIP_LUT (0)
#endif

/* Saturation handling: 0=hard mask (sat->0), 1=soft attenuation near saturation. */
#ifndef PQ128_USE_SAT_SOFTMASK
#define PQ128_USE_SAT_SOFTMASK (1)
#endif

/* Start attenuating when intensity approaches saturation threshold. */
#ifndef PQ128_SAT_SOFT_START
#define PQ128_SAT_SOFT_START (140)
#endif

/* Softmask curve: 0=linear, 1=quadratic (stronger suppression near saturation). */
#ifndef PQ128_SAT_SOFT_POWER2
#define PQ128_SAT_SOFT_POWER2 (1)
#endif

/* Optional dark-region mask: suppress p/q when intensity is very low.
 * Helps when deep colors (e.g. dark blue/red) have low luma (Y) so gradients are noisy.
 */
#ifndef PQ128_USE_DARK_SOFTMASK
#define PQ128_USE_DARK_SOFTMASK (0)
#endif

/* Fully suppress when avg intensity <= TH. */
#ifndef PQ128_DARK_TH
#define PQ128_DARK_TH (4)
#endif

/* Full weight when avg intensity >= SOFT_END. */
#ifndef PQ128_DARK_SOFT_END
#define PQ128_DARK_SOFT_END (30)
#endif

/* Softmask curve: 0=linear, 1=quadratic (stronger suppression near dark). */
#ifndef PQ128_DARK_SOFT_POWER2
#define PQ128_DARK_SOFT_POWER2 (1)
#endif

/* Optional chroma-edge mask: suppress p/q near strong chroma transitions.
 * This helps reduce false shape caused by albedo/color boundaries.
 *
 * NOTE: UYVY is 4:2:2 so chroma is shared per 2 pixels; we duplicate per-pixel.
 */
#ifndef PQ128_USE_CHROMA_EDGEMASK
#if FC128_PROFILE_CLASSIC
#define PQ128_USE_CHROMA_EDGEMASK (0)
#else
#define PQ128_USE_CHROMA_EDGEMASK (1)
#endif
#endif

/* Start suppressing when chroma-edge |C(x+1)-C(x-1)| exceeds this. */
#ifndef PQ128_CHROMA_EDGE_START
#define PQ128_CHROMA_EDGE_START (16)
#endif

/* Fully suppress when chroma-edge reaches this. */
#ifndef PQ128_CHROMA_EDGE_TH
#define PQ128_CHROMA_EDGE_TH (48)
#endif

/* Curve: 0=linear, 1=quadratic (stronger suppression near threshold). */
#ifndef PQ128_CHROMA_EDGE_POWER2
#define PQ128_CHROMA_EDGE_POWER2 (1)
#endif

/* Optional focus/blur mask (defocus cue):
 * Compute focus = max(0, edge(I) - edge(blur(I))) and down-weight p/q where focus is low.
 * This is a cheap, texture-based confidence term that tends to stabilize results when the
 * image is motion-blurred or out-of-focus.
 */
#ifndef PQ128_USE_FOCUS_SOFTMASK
#if FC128_PROFILE_CLASSIC
#define PQ128_USE_FOCUS_SOFTMASK (0)
#else
#define PQ128_USE_FOCUS_SOFTMASK (1)
#endif
#endif

/* Fully suppress when focus <= TH. */
#ifndef PQ128_FOCUS_TH
#define PQ128_FOCUS_TH (8)
#endif

/* Full weight when focus >= SOFT_END. */
#ifndef PQ128_FOCUS_SOFT_END
#define PQ128_FOCUS_SOFT_END (40)
#endif

/* Curve: 0=linear, 1=quadratic (stronger suppression near threshold). */
#ifndef PQ128_FOCUS_POWER2
#define PQ128_FOCUS_POWER2 (1)
#endif

/* Denominator range: (I0+I1+eps), I in [0..255] */
#ifndef PQ128_DEN_MAX
#define PQ128_DEN_MAX (510 + PQ128_NORM_EPS)
#endif

/* Optional highlight compression (tone knee) before computing p/q.
 * This reduces the impact of near-saturation highlights without adding per-pixel floating point.
 */
#ifndef PQ128_USE_INTENSITY_KNEE
#if FC128_PROFILE_CLASSIC
#define PQ128_USE_INTENSITY_KNEE (0)
#else
#define PQ128_USE_INTENSITY_KNEE (1)
#endif
#endif

/* Optional gamma lift on intensity before computing p/q.
 * Useful to lift dark objects (e.g. deep blue with low luma Y) so epsilon does not dominate.
 * Implemented as a 256-entry LUT built once; no per-pixel floating point.
 */
#ifndef PQ128_USE_INTENSITY_GAMMA
#if FC128_PROFILE_CLASSIC
#define PQ128_USE_INTENSITY_GAMMA (0)
#else
#define PQ128_USE_INTENSITY_GAMMA (1)
#endif
#endif

/* Gamma applied to normalized intensity in [0..1].
 * gamma < 1 lifts shadows, gamma > 1 darkens shadows.
 */
#ifndef PQ128_INTENSITY_GAMMA
#define PQ128_INTENSITY_GAMMA (0.75f)
#endif

/* Knee start (0..255). Values above this are compressed. */
#ifndef PQ128_KNEE_START
#define PQ128_KNEE_START (195)
#endif

/* Compression strength as right shift: 2 => /4, 3 => /8 (stronger). */
#ifndef PQ128_KNEE_SHIFT
#define PQ128_KNEE_SHIFT (4)
#endif

/* Edge taper (window) to reduce FFT wrap-around artifacts. */
#ifndef PQ128_USE_TAPER
#if FC128_PROFILE_CLASSIC
#define PQ128_USE_TAPER (0)
#else
#define PQ128_USE_TAPER (1)
#endif
#endif

/* Width (pixels) of the taper region from each edge. 0 disables taper. */
#ifndef PQ128_TAPER_WIDTH
#define PQ128_TAPER_WIDTH (16)
#endif

/* Published when a full p/q write completes.
 * g_pq128_seq == g_video_frame_seq used for computation.
 */
volatile uint32_t g_pq128_seq = 0;
volatile uint32_t g_pq128_base_offset = 0;

/* Published when a full depth frame write completes. */
volatile uint32_t g_depth_seq = 0;
volatile uint32_t g_depth_base_offset = 0;

#ifndef ENABLE_FC128_DEPTH
#define ENABLE_FC128_DEPTH 1
#endif

/* One-time FC128 layout log (xprintf). Disable to avoid any printf overhead. */
#ifndef FC128_LAYOUT_LOG_ENABLE
#define FC128_LAYOUT_LOG_ENABLE (0)
#endif

// ---- FC(FFT) scratch layout (float planes) ----
/*
 * FC integration grid size:
 * - FC_RESULT_N is the depth map size we finally export (kept at 128).
 * - FC_FFT_N selects the FFT grid size. When set to 256, we zero-pad the 128x128
 *   p/q into the center of a 256x256 plane, run FC on 256, then crop the center
 *   128x128 from Z for export.
 */
#define FC_RESULT_N (128)

#ifndef FC_FFT_N
#define FC_FFT_N (128) // 128 or 256
#endif

#if (FC_FFT_N != 128) && (FC_FFT_N != 256)
#error "FC_FFT_N must be 128 or 256"
#endif

#if (FC_FFT_N < FC_RESULT_N)
#error "FC_FFT_N must be >= FC_RESULT_N"
#endif

#define FC_FFT_PAD ((FC_FFT_N - FC_RESULT_N) / 2)

/*
 * Padding placement for FC_FFT_N > FC_RESULT_N:
 * - 1 (default): center the 128x128 block in the FFT plane (symmetric zero border)
 * - 0: place the 128x128 block at (0,0) and pad only to the right/bottom
 *
 * If 256-FFT output looks like speckle/noise, toggling this helps determine whether
 * the issue is related to spatial shift/phase handling vs. other causes.
 */
#ifndef FC_PAD_CENTERED
#define FC_PAD_CENTERED (0)
#endif

#if FC_PAD_CENTERED
#define FC_PAD_X0 (FC_FFT_PAD)
#define FC_PAD_Y0 (FC_FFT_PAD)
#else
#define FC_PAD_X0 (0)
#define FC_PAD_Y0 (0)
#endif

#define FC128_PLANE_BYTES ((uint32_t)(FC_FFT_N * FC_FFT_N * (uint32_t)sizeof(float)))

/*
 * IMPORTANT:
 * Do NOT overlap scratch with the exported 320x240 depth buffer.
 * Depth occupies [DEPTH_OFFSET, DEPTH_OFFSET + DEPTH_BYTES).
 */
#define FC128_OFFSET_BASE ALIGN16_U32(DEPTH_OFFSET + DEPTH_BYTES)

#define FC128_P_REAL (FC128_OFFSET_BASE + 0U * FC128_PLANE_BYTES)
#define FC128_P_IMAG (FC128_OFFSET_BASE + 1U * FC128_PLANE_BYTES)
#define FC128_Q_REAL (FC128_OFFSET_BASE + 2U * FC128_PLANE_BYTES)
#define FC128_Q_IMAG (FC128_OFFSET_BASE + 3U * FC128_PLANE_BYTES)

#define FC128_P_HAT_REAL (FC128_OFFSET_BASE + 4U * FC128_PLANE_BYTES)
#define FC128_P_HAT_IMAG (FC128_OFFSET_BASE + 5U * FC128_PLANE_BYTES)
#define FC128_Q_HAT_REAL (FC128_OFFSET_BASE + 6U * FC128_PLANE_BYTES)
#define FC128_Q_HAT_IMAG (FC128_OFFSET_BASE + 7U * FC128_PLANE_BYTES)

#define FC128_Z_HAT_REAL (FC128_OFFSET_BASE + 8U * FC128_PLANE_BYTES)
#define FC128_Z_HAT_IMAG (FC128_OFFSET_BASE + 9U * FC128_PLANE_BYTES)

#define FC128_Z_REAL (FC128_OFFSET_BASE + 10U * FC128_PLANE_BYTES)
#define FC128_Z_IMAG (FC128_OFFSET_BASE + 11U * FC128_PLANE_BYTES)

#define FC128_TMP_REAL (FC128_OFFSET_BASE + 12U * FC128_PLANE_BYTES)
#define FC128_TMP_IMAG (FC128_OFFSET_BASE + 13U * FC128_PLANE_BYTES)

#if FC128_GNG_ENABLE
static void fc128_gng_update_from_frame(uint32_t frame_base_offset, uint32_t frame_seq)
{
    float row_z[FC_RESULT_N];
    int updates = 0;

#if (PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_FOCUS_PLANE)
    {
        uint8_t focus_row[FC_RESULT_N];
#if PQ128_STORE_EDGE_PLANE
        uint8_t edge_row[FC_RESULT_N];
#endif
#if PQ128_STORE_EDGE_BLUR_PLANE
        uint8_t edge_blur_row[FC_RESULT_N];
#endif
#if PQ128_STORE_SATMASK_PLANE
        uint8_t satmask_row[FC_RESULT_N];
#endif

        for (int y = 0; y < FC_RESULT_N; y += FC128_GNG_SAMPLE_STRIDE)
        {
#if FC128_GNG_SKIP_PADDED_PQ128_SAMPLES
            const int src_y = PQ128_Y0 + y * PQ128_SAMPLE_STRIDE_Y;
            if ((src_y < 0) || (src_y >= FRAME_HEIGHT))
            {
                continue;
            }
#endif
            (void)hyperram_b_read(focus_row,
                                  (void *)(frame_base_offset + PQ128_FOCUS_OFFSET + (uint32_t)y * (uint32_t)PQ128_SIZE),
                                  (uint32_t)sizeof(focus_row));

#if PQ128_STORE_EDGE_PLANE
            (void)hyperram_b_read(edge_row,
                                  (void *)(frame_base_offset + PQ128_EDGE_OFFSET + (uint32_t)y * (uint32_t)PQ128_SIZE),
                                  (uint32_t)sizeof(edge_row));
#endif

#if PQ128_STORE_EDGE_BLUR_PLANE
            (void)hyperram_b_read(edge_blur_row,
                                  (void *)(frame_base_offset + PQ128_EDGE_BLUR_OFFSET + (uint32_t)y * (uint32_t)PQ128_SIZE),
                                  (uint32_t)sizeof(edge_blur_row));
#endif

#if PQ128_STORE_SATMASK_PLANE
            (void)hyperram_b_read(satmask_row,
                                  (void *)(frame_base_offset + PQ128_SATMASK_OFFSET + (uint32_t)y * (uint32_t)PQ128_SIZE),
                                  (uint32_t)sizeof(satmask_row));
#endif

#if (FC128_GNG_Z_SOURCE == 0)
            const uint32_t z_base = (uint32_t)((y + FC_PAD_Y0) * FC_FFT_N + FC_PAD_X0) * (uint32_t)sizeof(float);
            (void)hyperram_b_read(row_z, (void *)(frame_base_offset + FC128_Z_REAL + z_base), (uint32_t)sizeof(row_z));
#endif

            for (int x = 0; x < FC_RESULT_N; x += FC128_GNG_SAMPLE_STRIDE)
            {
#if FC128_GNG_SKIP_PADDED_PQ128_SAMPLES
                const int src_x = PQ128_X0 + x * PQ128_SAMPLE_STRIDE_X;
                if ((src_x < 0) || (src_x >= FRAME_WIDTH))
                {
                    continue;
                }
#endif
#if PQ128_STORE_SATMASK_PLANE
                if (satmask_row[x] == 0)
                {
                    continue;
                }
#endif
                const uint8_t f_u8 = focus_row[x];

#if PQ128_STORE_EDGE_PLANE
                const uint32_t e = (uint32_t)edge_row[x];
                if (e < (uint32_t)FC128_GNG_EDGE_TH)
                {
                    continue;
                }

#if (FC128_GNG_GATE_MODE == 0)
                /* Sharp-edge sampling: focus high. */
                if (f_u8 < (uint8_t)FC128_GNG_FOCUS_TH)
                {
                    continue;
                }
#else
                /* Blurred-edge sampling: edge exists but focus is low vs edge strength.
                 * Prefer stored edge_blur when available.
                 */
#if PQ128_STORE_EDGE_BLUR_PLANE
                const int32_t edge_blur = (int32_t)edge_blur_row[x];
#else
                int32_t edge_blur = (int32_t)e - (int32_t)f_u8;
                if (edge_blur < 0)
                {
                    edge_blur = 0;
                }
#endif
                if ((((int64_t)edge_blur) << 15) < ((int64_t)FC128_GNG_BLUR_RATIO_MIN_Q15 * (int64_t)e))
                {
                    continue;
                }
#endif
#else
                /* No edge plane: fall back to simple focus threshold. */
                if (f_u8 < (uint8_t)FC128_GNG_FOCUS_TH)
                {
                    continue;
                }
#endif

                float z;
#if (FC128_GNG_Z_SOURCE == 0)
                z = row_z[x];
                if (!isfinite(z))
                {
                    continue;
                }
#else
                /* Pseudo depth from defocus proxy (focus plane). */
                const int f = (int)f_u8;
#if (FC128_GNG_FOCUS_Z_INVERT != 0)
                z = (float)(255 - f) * (float)FC128_GNG_FOCUS_Z_GAIN;
#else
                z = (float)(f) * (float)FC128_GNG_FOCUS_Z_GAIN;
#endif
#endif

                fc128_gng_step(&g_fc128_gng, (float)x, (float)y, z);
                updates++;
                if (updates >= FC128_GNG_MAX_UPDATES_PER_FRAME)
                {
                    y = FC_RESULT_N;
                    break;
                }
            }
        }
    }
#endif

/* If focus plane is available but yielded no samples, optionally seed from Z.
 * If focus plane is NOT available, keep seeding enabled so GNG can still run.
 */
#if (PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_FOCUS_PLANE)
    if ((updates == 0) && (FC128_GNG_ALLOW_Z_SEED_WHEN_NO_FOCUS != 0))
    {
#else
    if (updates == 0)
    {
#endif
        static const uint8_t pts[8][2] = {
            {64, 64},
            {32, 64},
            {96, 64},
            {64, 32},
            {64, 96},
            {32, 32},
            {96, 32},
            {32, 96},
        };

        for (int k = 0; k < 8; k++)
        {
            const int x = (int)pts[k][0];
            const int y = (int)pts[k][1];

            const uint32_t z_base = (uint32_t)((y + FC_PAD_Y0) * FC_FFT_N + FC_PAD_X0) * (uint32_t)sizeof(float);
            (void)hyperram_b_read(row_z, (void *)(frame_base_offset + FC128_Z_REAL + z_base), (uint32_t)sizeof(row_z));
            const float z = row_z[x];
            if (!isfinite(z))
            {
                continue;
            }
            fc128_gng_step(&g_fc128_gng, (float)x, (float)y, z);
            updates++;
        }
    }

#if (FC128_GNG_LOG_PERIOD > 0)
    if ((frame_seq % (uint32_t)FC128_GNG_LOG_PERIOD) == 0U)
    {
        const int nodes = fc128_gng_count_used(&g_fc128_gng);
        xprintf("[GNG] seq=%lu updates=%d nodes=%d\n", (unsigned long)frame_seq, updates, nodes);
    }
#endif

    if ((FC128_GNG_MAINT_PERIOD_FRAMES > 0U) && ((frame_seq % (uint32_t)FC128_GNG_MAINT_PERIOD_FRAMES) == 0U))
    {
        fc128_gng_maint(&g_fc128_gng);
    }
}
#endif /* FC128_GNG_ENABLE */

/* Total FC scratch footprint relative to frame_base_offset. */
#define FC128_SCRATCH_END (FC128_OFFSET_BASE + 14U * FC128_PLANE_BYTES)

static void fc128_layout_check_once(uint32_t frame_base_offset)
{
    static bool s_printed = false;
    if (s_printed)
    {
        return;
    }
    s_printed = true;

    const uint32_t abs_end = frame_base_offset + (uint32_t)FC128_SCRATCH_END;

#if FC128_LAYOUT_LOG_ENABLE
    xprintf("[FC128] FC_FFT_N=%d FC_RESULT_N=%d pad=%d centered=%d\n",
            (int)FC_FFT_N,
            (int)FC_RESULT_N,
            (int)FC_FFT_PAD,
            (int)FC_PAD_CENTERED);
    xprintf("[FC128] base=0x%08lX end=0x%08lX (HYPERRAM_SIZE=0x%08lX)\n",
            (unsigned long)frame_base_offset,
            (unsigned long)abs_end,
            (unsigned long)HYPERRAM_SIZE);
    xprintf("[FC128] plane_bytes=%lu scratch_end(rel)=%lu\n",
            (unsigned long)FC128_PLANE_BYTES,
            (unsigned long)FC128_SCRATCH_END);
#endif

    if (abs_end > (uint32_t)HYPERRAM_SIZE)
    {
#if FC128_LAYOUT_LOG_ENABLE
        xprintf("[FC128] ERROR: scratch exceeds HyperRAM (abs_end=%lu)\n", (unsigned long)abs_end);
#endif
    }
}

/* Reduce compute time by halving forward 2D FFT cost:
 * Pack P into real and Q into imag, compute one complex FFT, then unpack:
 *   C = FFT(P + jQ)
 * For 2D indices (u,v), define neg index (-u,-v) as
 *   (uN,vN) = ((N-u)%N, (N-v)%N)
 * Then:
 *   P_hat = 0.5 * ( C + conj(Cneg) )
 *   Q_hat = ( C - conj(Cneg) ) / (2j)
 */
#ifndef FC128_USE_PACKED_PQ_FFT
#define FC128_USE_PACKED_PQ_FFT (1)
#endif

/* If enabled, build Z_hat directly from packed spectrum C=FFT(P+jQ), avoiding
 * writing/reading P_hat/Q_hat planes (bandwidth saver).
 */
#ifndef FC128_PACKED_DIRECT_ZHAT
#define FC128_PACKED_DIRECT_ZHAT (1)
#endif

static FC128_UNUSED void fc128_unpack_pq_hats_from_packed(uint32_t frame_base_offset)
{
    float c_re_row[FC_FFT_N];
    float c_im_row[FC_FFT_N];
    float c_re_neg_row[FC_FFT_N];
    float c_im_neg_row[FC_FFT_N];

    float p_re_row[FC_FFT_N];
    float p_im_row[FC_FFT_N];
    float q_re_row[FC_FFT_N];
    float q_im_row[FC_FFT_N];

    for (int y = 0; y < FC_FFT_N; y++)
    {
        int y_neg = (y == 0) ? 0 : (FC_FFT_N - y);
        uint32_t row_off = (uint32_t)y * (uint32_t)FC_FFT_N * (uint32_t)sizeof(float);
        uint32_t row_off_neg = (uint32_t)y_neg * (uint32_t)FC_FFT_N * (uint32_t)sizeof(float);

        /* Packed spectrum lives in Z_HAT (temporary). */
        (void)hyperram_b_read(c_re_row, (void *)(frame_base_offset + FC128_Z_HAT_REAL + row_off), (uint32_t)sizeof(c_re_row));
        (void)hyperram_b_read(c_im_row, (void *)(frame_base_offset + FC128_Z_HAT_IMAG + row_off), (uint32_t)sizeof(c_im_row));
        (void)hyperram_b_read(c_re_neg_row, (void *)(frame_base_offset + FC128_Z_HAT_REAL + row_off_neg), (uint32_t)sizeof(c_re_neg_row));
        (void)hyperram_b_read(c_im_neg_row, (void *)(frame_base_offset + FC128_Z_HAT_IMAG + row_off_neg), (uint32_t)sizeof(c_im_neg_row));

        for (int x = 0; x < FC_FFT_N; x++)
        {
            int x_neg = (x == 0) ? 0 : (FC_FFT_N - x);
            float c_re = c_re_row[x];
            float c_im = c_im_row[x];
            float cn_re = c_re_neg_row[x_neg];
            float cn_im = c_im_neg_row[x_neg];

            /* conj(Cneg) = cn_re - j cn_im */
            p_re_row[x] = 0.5f * (c_re + cn_re);
            p_im_row[x] = 0.5f * (c_im - cn_im);
            q_re_row[x] = 0.5f * (c_im + cn_im);
            q_im_row[x] = 0.5f * (cn_re - c_re);
        }

        (void)hyperram_b_write(p_re_row, (void *)(frame_base_offset + FC128_P_HAT_REAL + row_off), (uint32_t)sizeof(p_re_row));
        (void)hyperram_b_write(p_im_row, (void *)(frame_base_offset + FC128_P_HAT_IMAG + row_off), (uint32_t)sizeof(p_im_row));
        (void)hyperram_b_write(q_re_row, (void *)(frame_base_offset + FC128_Q_HAT_REAL + row_off), (uint32_t)sizeof(q_re_row));
        (void)hyperram_b_write(q_im_row, (void *)(frame_base_offset + FC128_Q_HAT_IMAG + row_off), (uint32_t)sizeof(q_im_row));
    }
}

static void fc128_build_zhat_from_packed_spectrum(uint32_t frame_base_offset)
{
    const float two_pi = 6.2831853071795864769f;
    const float denom_eps = 1.0e-12f;

    /* Precompute uu and uu^2 once (FC_FFT_N is compile-time). */
    static bool s_uu_init = false;
    static float s_uu[FC_FFT_N];
    static float s_uu2[FC_FFT_N];
    if (!s_uu_init)
    {
        for (int x = 0; x < FC_FFT_N; x++)
        {
            int kk = (x < (FC_FFT_N / 2)) ? x : (x - FC_FFT_N);
            float uu = two_pi * (float)kk / (float)FC_FFT_N;
            s_uu[x] = uu;
            s_uu2[x] = uu * uu;
        }
        s_uu_init = true;
    }

    float c_re_y[FC_FFT_N];
    float c_im_y[FC_FFT_N];
    float c_re_yn[FC_FFT_N];
    float c_im_yn[FC_FFT_N];

    float z_re_y[FC_FFT_N];
    float z_im_y[FC_FFT_N];
    float z_re_yn[FC_FFT_N];
    float z_im_yn[FC_FFT_N];

    /* Process symmetric row pairs so we can overwrite Z_HAT in-place safely. */
    for (int y = 0; y <= (FC_FFT_N / 2); y++)
    {
        int y_neg = (y == 0) ? 0 : (FC_FFT_N - y);

        uint32_t row_off = (uint32_t)y * (uint32_t)FC_FFT_N * (uint32_t)sizeof(float);
        uint32_t row_off_neg = (uint32_t)y_neg * (uint32_t)FC_FFT_N * (uint32_t)sizeof(float);

        /* Packed spectrum C is currently stored in Z_HAT. */
        (void)hyperram_b_read(c_re_y, (void *)(frame_base_offset + FC128_Z_HAT_REAL + row_off), (uint32_t)sizeof(c_re_y));
        (void)hyperram_b_read(c_im_y, (void *)(frame_base_offset + FC128_Z_HAT_IMAG + row_off), (uint32_t)sizeof(c_im_y));

        if (y_neg == y)
        {
            memcpy(c_re_yn, c_re_y, sizeof(c_re_yn));
            memcpy(c_im_yn, c_im_y, sizeof(c_im_yn));
        }
        else
        {
            (void)hyperram_b_read(c_re_yn, (void *)(frame_base_offset + FC128_Z_HAT_REAL + row_off_neg), (uint32_t)sizeof(c_re_yn));
            (void)hyperram_b_read(c_im_yn, (void *)(frame_base_offset + FC128_Z_HAT_IMAG + row_off_neg), (uint32_t)sizeof(c_im_yn));
        }

        int ll = (y < (FC_FFT_N / 2)) ? y : (y - FC_FFT_N);
        float vv = two_pi * (float)ll / (float)FC_FFT_N;
        float vv2 = vv * vv;

        int ll_neg = (y_neg < (FC_FFT_N / 2)) ? y_neg : (y_neg - FC_FFT_N);
        float vv_neg = two_pi * (float)ll_neg / (float)FC_FFT_N;
        float vv2_neg = vv_neg * vv_neg;

#if USE_HELIUM_MVE
        {
            float32x4_t v_half = vdupq_n_f32(0.5f);
            float32x4_t v_vv = vdupq_n_f32(vv);
            float32x4_t v_vv2 = vdupq_n_f32(vv2);
            float32x4_t v_vv_neg = vdupq_n_f32(vv_neg);
            float32x4_t v_vv2_neg = vdupq_n_f32(vv2_neg);

            /* Handle DC (x=0) separately so the vector loop never sees k==0,
             * enabling a cheaper reverse-load path for x_neg.
             */
            {
                int x = 0;
                int x_neg = 0;
                float uu = s_uu[x];

                float c_re = c_re_y[x];
                float c_im = c_im_y[x];
                float cn_re = c_re_yn[x_neg];
                float cn_im = c_im_yn[x_neg];

                float p_re = 0.5f * (c_re + cn_re);
                float p_im = 0.5f * (c_im - cn_im);
                float q_re = 0.5f * (c_im + cn_im);
                float q_im = 0.5f * (cn_re - c_re);

                float denom = s_uu2[x] + vv2;
                if (denom < denom_eps)
                {
                    denom = 1.0f;
                }
                float real_num = uu * p_im + vv * q_im;
                float imag_num = -(uu * p_re + vv * q_re);
                z_re_y[x] = real_num / denom;
                z_im_y[x] = imag_num / denom;

                if (y_neg != y)
                {
                    float c2_re = c_re_yn[x];
                    float c2_im = c_im_yn[x];
                    float cn2_re = c_re_y[x_neg];
                    float cn2_im = c_im_y[x_neg];

                    float p2_re = 0.5f * (c2_re + cn2_re);
                    float p2_im = 0.5f * (c2_im - cn2_im);
                    float q2_re = 0.5f * (c2_im + cn2_im);
                    float q2_im = 0.5f * (cn2_re - c2_re);

                    float denom2 = s_uu2[x] + vv2_neg;
                    if (denom2 < denom_eps)
                    {
                        denom2 = 1.0f;
                    }

                    float real_num2 = uu * p2_im + vv_neg * q2_im;
                    float imag_num2 = -(uu * p2_re + vv_neg * q2_re);
                    z_re_yn[x] = real_num2 / denom2;
                    z_im_yn[x] = imag_num2 / denom2;
                }
            }

            int x = 1;
            for (; x + 3 < FC_FFT_N; x += 4)
            {
                float32x4_t v_uu = vld1q_f32(&s_uu[x]);
                float32x4_t v_uu2 = vld1q_f32(&s_uu2[x]);

                /* ---- Row y ---- */
                float32x4_t v_c_re = vld1q_f32(&c_re_y[x]);
                float32x4_t v_c_im = vld1q_f32(&c_im_y[x]);

                /* cn lanes are c_yn[x_neg] where x_neg = N-(x+i) for i=0..3.
                 * Load them as a contiguous block from the tail and reverse lanes.
                 */
                float32x4_t v_cn_re;
                float32x4_t v_cn_im;
                {
                    float32x4_t v_cn_re_rev = vld1q_f32(&c_re_yn[FC_FFT_N - (x + 3)]);
                    float32x4_t v_cn_im_rev = vld1q_f32(&c_im_yn[FC_FFT_N - (x + 3)]);
                    float t_re[4];
                    float t_im[4];
                    float cn_re_s[4];
                    float cn_im_s[4];
                    vst1q_f32(t_re, v_cn_re_rev);
                    vst1q_f32(t_im, v_cn_im_rev);
                    cn_re_s[0] = t_re[3];
                    cn_re_s[1] = t_re[2];
                    cn_re_s[2] = t_re[1];
                    cn_re_s[3] = t_re[0];
                    cn_im_s[0] = t_im[3];
                    cn_im_s[1] = t_im[2];
                    cn_im_s[2] = t_im[1];
                    cn_im_s[3] = t_im[0];
                    v_cn_re = vld1q_f32(cn_re_s);
                    v_cn_im = vld1q_f32(cn_im_s);
                }

                float32x4_t v_p_re = vmulq_f32(v_half, vaddq_f32(v_c_re, v_cn_re));
                float32x4_t v_p_im = vmulq_f32(v_half, vsubq_f32(v_c_im, v_cn_im));
                float32x4_t v_q_re = vmulq_f32(v_half, vaddq_f32(v_c_im, v_cn_im));
                float32x4_t v_q_im = vmulq_f32(v_half, vsubq_f32(v_cn_re, v_c_re));

                float32x4_t v_denom = vaddq_f32(v_uu2, v_vv2);

                /* real_num = uu*p_im + vv*q_im */
                float32x4_t v_real = vmulq_f32(v_uu, v_p_im);
                v_real = vfmaq_f32(v_real, v_q_im, v_vv);

                /* imag_num = -(uu*p_re + vv*q_re) */
                float32x4_t v_imag = vmulq_f32(v_uu, v_p_re);
                v_imag = vfmaq_f32(v_imag, v_q_re, v_vv);
                v_imag = vnegq_f32(v_imag);
                {
                    float denom4[4];
                    float real4[4];
                    float imag4[4];
                    vst1q_f32(denom4, v_denom);
                    vst1q_f32(real4, v_real);
                    vst1q_f32(imag4, v_imag);
                    for (int i = 0; i < 4; i++)
                    {
                        /* For x>=1, denom is strictly > 0 (uu^2 + vv^2). */
                        float inv = 1.0f / denom4[i];
                        z_re_y[x + i] = real4[i] * inv;
                        z_im_y[x + i] = imag4[i] * inv;
                    }
                }

                /* ---- Row y_neg (if different) ---- */
                if (y_neg != y)
                {
                    float32x4_t v_c2_re = vld1q_f32(&c_re_yn[x]);
                    float32x4_t v_c2_im = vld1q_f32(&c_im_yn[x]);

                    float32x4_t v_cn2_re;
                    float32x4_t v_cn2_im;
                    {
                        float32x4_t v_cn2_re_rev = vld1q_f32(&c_re_y[FC_FFT_N - (x + 3)]);
                        float32x4_t v_cn2_im_rev = vld1q_f32(&c_im_y[FC_FFT_N - (x + 3)]);
                        float t2_re[4];
                        float t2_im[4];
                        float cn2_re_s[4];
                        float cn2_im_s[4];
                        vst1q_f32(t2_re, v_cn2_re_rev);
                        vst1q_f32(t2_im, v_cn2_im_rev);
                        cn2_re_s[0] = t2_re[3];
                        cn2_re_s[1] = t2_re[2];
                        cn2_re_s[2] = t2_re[1];
                        cn2_re_s[3] = t2_re[0];
                        cn2_im_s[0] = t2_im[3];
                        cn2_im_s[1] = t2_im[2];
                        cn2_im_s[2] = t2_im[1];
                        cn2_im_s[3] = t2_im[0];
                        v_cn2_re = vld1q_f32(cn2_re_s);
                        v_cn2_im = vld1q_f32(cn2_im_s);
                    }

                    float32x4_t v_p2_re = vmulq_f32(v_half, vaddq_f32(v_c2_re, v_cn2_re));
                    float32x4_t v_p2_im = vmulq_f32(v_half, vsubq_f32(v_c2_im, v_cn2_im));
                    float32x4_t v_q2_re = vmulq_f32(v_half, vaddq_f32(v_c2_im, v_cn2_im));
                    float32x4_t v_q2_im = vmulq_f32(v_half, vsubq_f32(v_cn2_re, v_c2_re));

                    float32x4_t v_denom2 = vaddq_f32(v_uu2, v_vv2_neg);

                    float32x4_t v_real2 = vmulq_f32(v_uu, v_p2_im);
                    v_real2 = vfmaq_f32(v_real2, v_q2_im, v_vv_neg);

                    float32x4_t v_imag2 = vmulq_f32(v_uu, v_p2_re);
                    v_imag2 = vfmaq_f32(v_imag2, v_q2_re, v_vv_neg);
                    v_imag2 = vnegq_f32(v_imag2);
                    {
                        float denom2_4[4];
                        float real2_4[4];
                        float imag2_4[4];
                        vst1q_f32(denom2_4, v_denom2);
                        vst1q_f32(real2_4, v_real2);
                        vst1q_f32(imag2_4, v_imag2);
                        for (int i = 0; i < 4; i++)
                        {
                            /* For x>=1, denom2 is strictly > 0 (uu^2 + vv_neg^2). */
                            float inv2 = 1.0f / denom2_4[i];
                            z_re_yn[x + i] = real2_4[i] * inv2;
                            z_im_yn[x + i] = imag2_4[i] * inv2;
                        }
                    }
                }
            }

            for (; x < FC_FFT_N; x++)
            {
                int x_neg = (x == 0) ? 0 : (FC_FFT_N - x);
                float uu = s_uu[x];

                float c_re = c_re_y[x];
                float c_im = c_im_y[x];
                float cn_re = c_re_yn[x_neg];
                float cn_im = c_im_yn[x_neg];

                float p_re = 0.5f * (c_re + cn_re);
                float p_im = 0.5f * (c_im - cn_im);
                float q_re = 0.5f * (c_im + cn_im);
                float q_im = 0.5f * (cn_re - c_re);

                float denom = s_uu2[x] + vv2;
                if (denom < denom_eps)
                {
                    denom = 1.0f;
                }

                float real_num = uu * p_im + vv * q_im;
                float imag_num = -(uu * p_re + vv * q_re);
                z_re_y[x] = real_num / denom;
                z_im_y[x] = imag_num / denom;

                if (y_neg != y)
                {
                    float c2_re = c_re_yn[x];
                    float c2_im = c_im_yn[x];
                    float cn2_re = c_re_y[x_neg];
                    float cn2_im = c_im_y[x_neg];

                    float p2_re = 0.5f * (c2_re + cn2_re);
                    float p2_im = 0.5f * (c2_im - cn2_im);
                    float q2_re = 0.5f * (c2_im + cn2_im);
                    float q2_im = 0.5f * (cn2_re - c2_re);

                    float denom2 = s_uu2[x] + vv2_neg;
                    if (denom2 < denom_eps)
                    {
                        denom2 = 1.0f;
                    }

                    float real_num2 = uu * p2_im + vv_neg * q2_im;
                    float imag_num2 = -(uu * p2_re + vv_neg * q2_re);
                    z_re_yn[x] = real_num2 / denom2;
                    z_im_yn[x] = imag_num2 / denom2;
                }
            }
        }
#else
        for (int x = 0; x < FC_FFT_N; x++)
        {
            int x_neg = (x == 0) ? 0 : (FC_FFT_N - x);
            float uu = s_uu[x];

            /* ---- Row y ----
             * cn is C(-u,-v) -> row y_neg, col x_neg
             */
            float c_re = c_re_y[x];
            float c_im = c_im_y[x];
            float cn_re = c_re_yn[x_neg];
            float cn_im = c_im_yn[x_neg];

            float p_re = 0.5f * (c_re + cn_re);
            float p_im = 0.5f * (c_im - cn_im);
            float q_re = 0.5f * (c_im + cn_im);
            float q_im = 0.5f * (cn_re - c_re);

            float denom = s_uu2[x] + vv2;
            if (denom < denom_eps)
            {
                denom = 1.0f;
            }

            float real_num = uu * p_im + vv * q_im;
            float imag_num = -(uu * p_re + vv * q_re);
            z_re_y[x] = real_num / denom;
            z_im_y[x] = imag_num / denom;

            /* ---- Row y_neg (if different) ----
             * cn2 is C(-u,-(-v)) = C(-u, v) -> row y, col x_neg
             */
            if (y_neg != y)
            {
                float c2_re = c_re_yn[x];
                float c2_im = c_im_yn[x];
                float cn2_re = c_re_y[x_neg];
                float cn2_im = c_im_y[x_neg];

                float p2_re = 0.5f * (c2_re + cn2_re);
                float p2_im = 0.5f * (c2_im - cn2_im);
                float q2_re = 0.5f * (c2_im + cn2_im);
                float q2_im = 0.5f * (cn2_re - c2_re);

                float denom2 = s_uu2[x] + vv2_neg;
                if (denom2 < denom_eps)
                {
                    denom2 = 1.0f;
                }

                float real_num2 = uu * p2_im + vv_neg * q2_im;
                float imag_num2 = -(uu * p2_re + vv_neg * q2_re);
                z_re_yn[x] = real_num2 / denom2;
                z_im_yn[x] = imag_num2 / denom2;
            }
        }
#endif

        /* Enforce DC to 0 (avoids tiny residuals from numerical paths). */
        if (y == 0)
        {
            z_re_y[0] = 0.0f;
            z_im_y[0] = 0.0f;
            if (y_neg != y)
            {
                z_re_yn[0] = 0.0f;
                z_im_yn[0] = 0.0f;
            }
        }

        /* Overwrite packed spectrum with Z_hat in place. */
        (void)hyperram_b_write(z_re_y, (void *)(frame_base_offset + FC128_Z_HAT_REAL + row_off), (uint32_t)sizeof(z_re_y));
        (void)hyperram_b_write(z_im_y, (void *)(frame_base_offset + FC128_Z_HAT_IMAG + row_off), (uint32_t)sizeof(z_im_y));

        if (y_neg != y)
        {
            (void)hyperram_b_write(z_re_yn, (void *)(frame_base_offset + FC128_Z_HAT_REAL + row_off_neg), (uint32_t)sizeof(z_re_yn));
            (void)hyperram_b_write(z_im_yn, (void *)(frame_base_offset + FC128_Z_HAT_IMAG + row_off_neg), (uint32_t)sizeof(z_im_yn));
        }
    }
}

static void fc128_build_float_planes_from_pq(uint32_t frame_base_offset)
{
    int16_t p_row_i16[FC_RESULT_N];
    int16_t q_row_i16[FC_RESULT_N];
    float row_f32[FC_FFT_N];
    float row_zero[FC_FFT_N];
    memset(row_zero, 0, sizeof(row_zero));

    for (int y = 0; y < FC_FFT_N; y++)
    {
        const bool in_center = (y >= FC_PAD_Y0) && (y < (FC_PAD_Y0 + FC_RESULT_N));
        uint32_t row_f32_off = (uint32_t)y * (uint32_t)FC_FFT_N * (uint32_t)sizeof(float);

        if (!in_center)
        {
            (void)hyperram_b_write(row_zero, (void *)(frame_base_offset + FC128_P_REAL + row_f32_off), (uint32_t)sizeof(row_zero));
            (void)hyperram_b_write(row_zero, (void *)(frame_base_offset + FC128_P_IMAG + row_f32_off), (uint32_t)sizeof(row_zero));
            (void)hyperram_b_write(row_zero, (void *)(frame_base_offset + FC128_Q_REAL + row_f32_off), (uint32_t)sizeof(row_zero));
            (void)hyperram_b_write(row_zero, (void *)(frame_base_offset + FC128_Q_IMAG + row_f32_off), (uint32_t)sizeof(row_zero));
            continue;
        }

        const int ry = y - FC_PAD_Y0;
        uint32_t row_i16_off = (uint32_t)ry * (uint32_t)FC_RESULT_N * (uint32_t)sizeof(int16_t);
        (void)hyperram_b_read(p_row_i16, (void *)(frame_base_offset + PQ128_P_OFFSET + row_i16_off), (uint32_t)sizeof(p_row_i16));
        (void)hyperram_b_read(q_row_i16, (void *)(frame_base_offset + PQ128_Q_OFFSET + row_i16_off), (uint32_t)sizeof(q_row_i16));

        memcpy(row_f32, row_zero, sizeof(row_f32));
        for (int x = 0; x < FC_RESULT_N; x++)
        {
            row_f32[FC_PAD_X0 + x] = (float)p_row_i16[x];
        }
        (void)hyperram_b_write(row_f32, (void *)(frame_base_offset + FC128_P_REAL + row_f32_off), (uint32_t)sizeof(row_f32));
        (void)hyperram_b_write(row_zero, (void *)(frame_base_offset + FC128_P_IMAG + row_f32_off), (uint32_t)sizeof(row_zero));

        memcpy(row_f32, row_zero, sizeof(row_f32));
        for (int x = 0; x < FC_RESULT_N; x++)
        {
            row_f32[FC_PAD_X0 + x] = (float)q_row_i16[x];
        }
        (void)hyperram_b_write(row_f32, (void *)(frame_base_offset + FC128_Q_REAL + row_f32_off), (uint32_t)sizeof(row_f32));
        (void)hyperram_b_write(row_zero, (void *)(frame_base_offset + FC128_Q_IMAG + row_f32_off), (uint32_t)sizeof(row_zero));
    }
}

static FC128_UNUSED void fc128_compute_zhat(uint32_t frame_base_offset)
{
    const float two_pi = 6.2831853071795864769f;

    /* Precompute uu and uu^2 once (FC_FFT_N is compile-time). */
    static bool s_uu_init = false;
    static float s_uu[FC_FFT_N];
    static float s_uu2[FC_FFT_N];
    if (!s_uu_init)
    {
        for (int x = 0; x < FC_FFT_N; x++)
        {
            int kk = (x < (FC_FFT_N / 2)) ? x : (x - FC_FFT_N);
            float uu = two_pi * (float)kk / (float)FC_FFT_N;
            s_uu[x] = uu;
            s_uu2[x] = uu * uu;
        }
        s_uu_init = true;
    }

    float p_re[FC_FFT_N];
    float p_im[FC_FFT_N];
    float q_re[FC_FFT_N];
    float q_im[FC_FFT_N];
    float z_re[FC_FFT_N];
    float z_im[FC_FFT_N];

    const float denom_eps = 1.0e-12f;

    for (int y = 0; y < FC_FFT_N; y++)
    {
        uint32_t row_off = (uint32_t)y * (uint32_t)FC_FFT_N * (uint32_t)sizeof(float);
        (void)hyperram_b_read(p_re, (void *)(frame_base_offset + FC128_P_HAT_REAL + row_off), (uint32_t)sizeof(p_re));
        (void)hyperram_b_read(p_im, (void *)(frame_base_offset + FC128_P_HAT_IMAG + row_off), (uint32_t)sizeof(p_im));
        (void)hyperram_b_read(q_re, (void *)(frame_base_offset + FC128_Q_HAT_REAL + row_off), (uint32_t)sizeof(q_re));
        (void)hyperram_b_read(q_im, (void *)(frame_base_offset + FC128_Q_HAT_IMAG + row_off), (uint32_t)sizeof(q_im));

        int ll = (y < (FC_FFT_N / 2)) ? y : (y - FC_FFT_N);
        float vv = two_pi * (float)ll / (float)FC_FFT_N;
        float vv2 = vv * vv;

#if USE_HELIUM_MVE
        {
            float32x4_t v_vv = vdupq_n_f32(vv);
            float32x4_t v_vv2 = vdupq_n_f32(vv2);

            int x = 0;
            for (; x + 3 < FC_FFT_N; x += 4)
            {
                float32x4_t v_uu = vld1q_f32(&s_uu[x]);
                float32x4_t v_uu2 = vld1q_f32(&s_uu2[x]);
                float32x4_t v_denom = vaddq_f32(v_uu2, v_vv2);

                float32x4_t v_pre = vld1q_f32(&p_re[x]);
                float32x4_t v_pim = vld1q_f32(&p_im[x]);
                float32x4_t v_qre = vld1q_f32(&q_re[x]);
                float32x4_t v_qim = vld1q_f32(&q_im[x]);

                /* real_num = uu*p_im + vv*q_im */
                float32x4_t v_real = vmulq_f32(v_uu, v_pim);
                v_real = vfmaq_f32(v_real, v_qim, v_vv);

                /* imag_num = -(uu*p_re + vv*q_re) */
                float32x4_t v_imag = vmulq_f32(v_uu, v_pre);
                v_imag = vfmaq_f32(v_imag, v_qre, v_vv);
                v_imag = vnegq_f32(v_imag);

                /* Some MVE toolchains do not provide reciprocal/max intrinsics.
                 * Keep MVE for mul/add, then do safe scalar denom clamp + division.
                 */
                float denom4[4];
                float real4[4];
                float imag4[4];
                vst1q_f32(denom4, v_denom);
                vst1q_f32(real4, v_real);
                vst1q_f32(imag4, v_imag);
                for (int i = 0; i < 4; i++)
                {
                    float denom = denom4[i];
                    if (denom < denom_eps)
                    {
                        denom = 1.0f;
                    }
                    z_re[x + i] = real4[i] / denom;
                    z_im[x + i] = imag4[i] / denom;
                }
            }

            for (; x < FC_FFT_N; x++)
            {
                float uu = s_uu[x];
                float denom = s_uu2[x] + vv2;
                if (denom < denom_eps)
                {
                    denom = 1.0f;
                }
                float real_num = uu * p_im[x] + vv * q_im[x];
                float imag_num = -(uu * p_re[x] + vv * q_re[x]);
                z_re[x] = real_num / denom;
                z_im[x] = imag_num / denom;
            }
        }
#else
        for (int x = 0; x < FC_FFT_N; x++)
        {
            float uu = s_uu[x];
            float denom = s_uu2[x] + vv2;
            if (denom < denom_eps)
            {
                denom = 1.0f;
            }

            /* Z_hat = (-j*uu*P_hat - j*vv*Q_hat) / (uu^2 + vv^2)
             * For A = a + j b, -j*uu*A = uu*b - j*(uu*a)
             */
            float real_num = uu * p_im[x] + vv * q_im[x];
            float imag_num = -(uu * p_re[x] + vv * q_re[x]);
            z_re[x] = real_num / denom;
            z_im[x] = imag_num / denom;
        }
#endif

        /* Enforce DC to 0 (avoids tiny residuals from numerical paths). */
        if (y == 0)
        {
            z_re[0] = 0.0f;
            z_im[0] = 0.0f;
        }

        (void)hyperram_b_write(z_re, (void *)(frame_base_offset + FC128_Z_HAT_REAL + row_off), (uint32_t)sizeof(z_re));
        (void)hyperram_b_write(z_im, (void *)(frame_base_offset + FC128_Z_HAT_IMAG + row_off), (uint32_t)sizeof(z_im));
    }
}

static void fc128_export_depth_u8_320x240(uint32_t frame_base_offset)
{
    /* Export placement: keep the 128x128 depth image centered in 320x240,
     * independent from PQ128 sampling region (which may extend beyond the frame
     * when strides are large and we zero-pad).
     */
    const int export_x0 = (FRAME_WIDTH - FC_RESULT_N) / 2;
    const int export_y0 = (FRAME_HEIGHT - FC_RESULT_N) / 2;

    float row_z[FC_RESULT_N];

#if FC128_EXPORT_FIXED_SCALE
    /* z_ref is applied to the fixed-scale mapping to stabilize the output.
     * We intentionally use the previous EMA value for the current frame mapping,
     * then update EMA at the end of this function.
     */
    static float s_zref_ema = 0.0f;
    static int s_zref_init = 0;
    const float z_ref = (FC128_EXPORT_USE_ZREF_EMA && s_zref_init) ? s_zref_ema : 0.0f;

    static float s_znear = (float)FC128_EXPORT_Z_NEAR;
    static float s_zfar = (float)FC128_EXPORT_Z_FAR;
#if FC128_EXPORT_FIXED_AUTOCALIB
    static uint32_t s_autocalib_left = FC128_EXPORT_FIXED_AUTOCALIB_FRAMES;
#endif
    float z_near = s_znear;
    float z_far = s_zfar;
    float span = z_near - z_far;
    float inv_span = (span > FC128_EXPORT_RANGE_FLOOR) ? (1.0f / span) : (1.0f / FC128_EXPORT_RANGE_FLOOR);

    double sum_z = 0.0;
    uint32_t sum_n = 0U;

    float z_adj_min = FLT_MAX;
    float z_adj_max = -FLT_MAX;
#else
    float z_min = FLT_MAX;
    float z_max = -FLT_MAX;

    for (int y = 0; y < FC_RESULT_N; y++)
    {
        /* Read center crop from the FC_FFT_N x FC_FFT_N Z plane. */
        const uint32_t base = (uint32_t)((y + FC_PAD_Y0) * FC_FFT_N + FC_PAD_X0) * (uint32_t)sizeof(float);
        (void)hyperram_b_read(row_z, (void *)(frame_base_offset + FC128_Z_REAL + base), (uint32_t)sizeof(row_z));

#if USE_HELIUM_MVE
        // MVE版: 4要素単位でロードし、スカラーでmin/max更新（ツールチェーン互換）
        {
            int x;
            for (x = 0; x < FC_RESULT_N - 3; x += 4)
            {
                float32x4_t vz = vld1q_f32(&row_z[x]);
                float t[4];
                vst1q_f32(t, vz);
                for (int i = 0; i < 4; i++)
                {
                    if (t[i] < z_min)
                        z_min = t[i];
                    if (t[i] > z_max)
                        z_max = t[i];
                }
            }
            for (; x < FC_RESULT_N; x++)
            {
                float v0 = row_z[x];
                if (v0 < z_min)
                    z_min = v0;
                if (v0 > z_max)
                    z_max = v0;
            }
        }
#else
        for (int x = 0; x < FC_RESULT_N; x++)
        {
            float v0 = row_z[x];
            if (v0 < z_min)
            {
                z_min = v0;
            }
            if (v0 > z_max)
            {
                z_max = v0;
            }
        }
#endif
    }

    float use_z_min = z_min;
    float use_z_max = z_max;

#if FC128_EXPORT_USE_ZMINMAX_EMA
    {
        static float s_zmin_ema = 0.0f;
        static float s_zmax_ema = 0.0f;
        static int s_ema_init = 0;
        if (!s_ema_init)
        {
            s_zmin_ema = z_min;
            s_zmax_ema = z_max;
            s_ema_init = 1;
        }
        else
        {
            const float k = 1.0f / (float)(1U << FC128_EXPORT_ZMINMAX_EMA_SHIFT);
            s_zmin_ema += (z_min - s_zmin_ema) * k;
            s_zmax_ema += (z_max - s_zmax_ema) * k;
        }
        use_z_min = s_zmin_ema;
        use_z_max = s_zmax_ema;
    }
#endif

    float range = use_z_max - use_z_min;
    if (range < FC128_EXPORT_RANGE_FLOOR)
    {
        range = FC128_EXPORT_RANGE_FLOOR;
    }

    const float inv_range = 1.0f / range;
#endif

    uint8_t line[FRAME_WIDTH];
    for (int y = 0; y < FRAME_HEIGHT; y++)
    {
        memset(line, (int)FC128_EXPORT_BG_U8, sizeof(line));

        if (y >= export_y0 && y < (export_y0 + FC_RESULT_N))
        {
            int ry = y - export_y0;
            const uint32_t base = (uint32_t)((ry + FC_PAD_Y0) * FC_FFT_N + FC_PAD_X0) * (uint32_t)sizeof(float);
            (void)hyperram_b_read(row_z, (void *)(frame_base_offset + FC128_Z_REAL + base), (uint32_t)sizeof(row_z));

#if USE_HELIUM_MVE
            {
#if FC128_EXPORT_FIXED_SCALE
                /* Fixed mapping: n = ( (z - z_ref) - Z_FAR ) / (Z_NEAR - Z_FAR)
                 * so that Z_NEAR => 1 (RED), Z_FAR => 0 (BLUE).
                 */
                float32x4_t vzref = vdupq_n_f32(z_ref);
                float32x4_t vzfar = vdupq_n_f32((float)FC128_EXPORT_Z_FAR);
                float32x4_t vscale = vdupq_n_f32(255.0f * inv_span);
#else
                float32x4_t vzmin = vdupq_n_f32(use_z_min);
                float32x4_t vscale = vdupq_n_f32(255.0f * inv_range);
#endif
                float32x4_t vmid = vdupq_n_f32(127.5f);
                float32x4_t vround = vdupq_n_f32(0.5f);

                int32x4_t vzero_i32 = vdupq_n_s32(0);
                int32x4_t vmax_i32 = vdupq_n_s32(255);
                int32x4_t vbias_i32 = vdupq_n_s32((int32_t)FC128_EXPORT_BRIGHTNESS_BIAS_U8);

                const bool use_contrast = (FC128_EXPORT_CONTRAST_Q15 != 32768);
                float32x4_t va = vdupq_n_f32((float)FC128_EXPORT_CONTRAST_Q15 / 32768.0f);

                for (int x = 0; x < FC_RESULT_N; x += 4)
                {
                    float32x4_t vz = vld1q_f32(&row_z[x]);
#if FC128_EXPORT_FIXED_SCALE
                    /* vf in [0..255] domain */
                    float32x4_t vf = vmulq_f32(vsubq_f32(vsubq_f32(vz, vzref), vzfar), vscale);

                    /* Accumulate mean for z_ref update (scalar-friendly). */
                    {
                        float t[4];
                        vst1q_f32(t, vz);
                        sum_z += (double)t[0] + (double)t[1] + (double)t[2] + (double)t[3];
                        sum_n += 4U;

                        const float a0 = t[0] - z_ref;
                        const float a1 = t[1] - z_ref;
                        const float a2 = t[2] - z_ref;
                        const float a3 = t[3] - z_ref;
                        if (a0 < z_adj_min)
                            z_adj_min = a0;
                        if (a1 < z_adj_min)
                            z_adj_min = a1;
                        if (a2 < z_adj_min)
                            z_adj_min = a2;
                        if (a3 < z_adj_min)
                            z_adj_min = a3;
                        if (a0 > z_adj_max)
                            z_adj_max = a0;
                        if (a1 > z_adj_max)
                            z_adj_max = a1;
                        if (a2 > z_adj_max)
                            z_adj_max = a2;
                        if (a3 > z_adj_max)
                            z_adj_max = a3;
                    }
#else
                    // vf = (z - zmin) * (255/range)
                    float32x4_t vf = vmulq_f32(vsubq_f32(vz, vzmin), vscale);
#endif

                    // Optional contrast around 127.5 in [0..255] domain
                    if (use_contrast)
                    {
                        vf = vaddq_f32(vmulq_f32(vsubq_f32(vf, vmid), va), vmid);
                    }

                    // Round and convert
                    vf = vaddq_f32(vf, vround);
                    int32x4_t vi = vcvtq_s32_f32(vf);

                    // Clamp: 0..255
                    vi = vmaxq_s32(vi, vzero_i32);
                    vi = vminq_s32(vi, vmax_i32);

#if FC128_EXPORT_INVERT
                    vi = vsubq_s32(vmax_i32, vi);
#endif

                    // Brightness bias + clamp (after invert)
                    if (FC128_EXPORT_BRIGHTNESS_BIAS_U8 != 0)
                    {
                        vi = vaddq_s32(vi, vbias_i32);
                        vi = vmaxq_s32(vi, vzero_i32);
                        vi = vminq_s32(vi, vmax_i32);
                    }

                    int32_t out_i[4];
                    vst1q_s32(out_i, vi);
                    line[export_x0 + x + 0] = (uint8_t)out_i[0];
                    line[export_x0 + x + 1] = (uint8_t)out_i[1];
                    line[export_x0 + x + 2] = (uint8_t)out_i[2];
                    line[export_x0 + x + 3] = (uint8_t)out_i[3];
                }
            }
#else
            for (int x = 0; x < FC_RESULT_N; x++)
            {
#if FC128_EXPORT_FIXED_SCALE
                const float z = row_z[x];
                sum_z += (double)z;
                sum_n++;

                const float z_adj = z - z_ref;
                if (z_adj < z_adj_min)
                    z_adj_min = z_adj;
                if (z_adj > z_adj_max)
                    z_adj_max = z_adj;

                float n = (z_adj - z_far) * inv_span;
#else
                float n = (row_z[x] - use_z_min) * inv_range;
#endif
                if (n < 0.0f)
                    n = 0.0f;
                if (n > 1.0f)
                    n = 1.0f;

                /* Optional contrast reduction around mid-gray to suppress "relative depth". */
                if (FC128_EXPORT_CONTRAST_Q15 != 32768)
                {
                    const float a = (float)FC128_EXPORT_CONTRAST_Q15 / 32768.0f;
                    n = (n - 0.5f) * a + 0.5f;
                    if (n < 0.0f)
                        n = 0.0f;
                    if (n > 1.0f)
                        n = 1.0f;
                }

                int out = (int)(n * 255.0f + 0.5f);
                if (out < 0)
                    out = 0;
                if (out > 255)
                    out = 255;

#if FC128_EXPORT_INVERT
                out = 255 - out;
#endif

                if (FC128_EXPORT_BRIGHTNESS_BIAS_U8 != 0)
                {
                    out += (int)FC128_EXPORT_BRIGHTNESS_BIAS_U8;
                    if (out < 0)
                        out = 0;
                    if (out > 255)
                        out = 255;
                }
                line[export_x0 + x] = (uint8_t)out;
            }
#endif
        }

        (void)hyperram_b_write(line, (void *)(frame_base_offset + DEPTH_OFFSET + (uint32_t)y * (uint32_t)FRAME_WIDTH), (uint32_t)sizeof(line));
    }

#if FC128_EXPORT_FIXED_SCALE
#if FC128_EXPORT_FIXED_AUTOCALIB
    /* Update fixed range for the next frames during initial calibration window. */
    if (s_autocalib_left != 0U && z_adj_min < z_adj_max)
    {
        float obs_span = z_adj_max - z_adj_min;
        if (obs_span < FC128_EXPORT_RANGE_FLOOR)
        {
            obs_span = FC128_EXPORT_RANGE_FLOOR;
        }
        const float m = (float)FC128_EXPORT_FIXED_AUTOCALIB_MARGIN;
        const float new_far = z_adj_min - obs_span * m;
        const float new_near = z_adj_max + obs_span * m;

        /* Expand only (never shrink) during calibration to avoid flicker. */
        if (new_far < s_zfar)
        {
            s_zfar = new_far;
        }
        if (new_near > s_znear)
        {
            s_znear = new_near;
        }

        s_autocalib_left--;
    }
#endif

#if FC128_EXPORT_USE_ZREF_EMA
    if (sum_n != 0U)
    {
        const float mean_z = (float)(sum_z / (double)sum_n);
        if (!s_zref_init)
        {
            s_zref_ema = mean_z;
            s_zref_init = 1;
        }
        else
        {
            const float k = 1.0f / (float)(1U << FC128_EXPORT_ZREF_EMA_SHIFT);
            s_zref_ema += (mean_z - s_zref_ema) * k;
        }
    }
#else
    (void)sum_z;
    (void)sum_n;
    if (!s_zref_init)
    {
        s_zref_ema = 0.0f;
        s_zref_init = 1;
    }
#endif
#endif
}

static void fc128_compute_depth_and_store(uint32_t frame_base_offset, uint32_t frame_seq)
{
    g_depth_seq = 0;

#if FC128_TIMING_ENABLE
    fc128_dwt_init_once();
    uint32_t t0 = fc128_dwt_now();
    uint32_t t_fft_p = t0;
    uint32_t t_fft_q = t0;
#endif

    fc128_layout_check_once(frame_base_offset);
    if ((frame_base_offset + (uint32_t)FC128_SCRATCH_END) > (uint32_t)HYPERRAM_SIZE)
    {
        /* Avoid corrupting memory if configuration/layout is invalid. */
        return;
    }

    fc128_build_float_planes_from_pq(frame_base_offset);

#if FC128_TIMING_ENABLE
    uint32_t t_build = fc128_dwt_now();
#endif

    /* FFT(P) and FFT(Q)
     * - Packed path: FFT(P + jQ) once.
     *   - Default: build Z_hat directly from packed spectrum (saves bandwidth).
     *   - Fallback: unpack P_hat/Q_hat then run fc128_compute_zhat().
     * - Non-packed path: run two separate FFTs then fc128_compute_zhat().
     */
#if FC128_USE_PACKED_PQ_FFT
    fft_2d_hyperram_full(
        frame_base_offset + FC128_P_REAL,
        frame_base_offset + FC128_Q_REAL,
        frame_base_offset + FC128_Z_HAT_REAL,
        frame_base_offset + FC128_Z_HAT_IMAG,
        frame_base_offset + FC128_TMP_REAL,
        frame_base_offset + FC128_TMP_IMAG,
        FC_FFT_N, FC_FFT_N, false);

#if FC128_TIMING_ENABLE
    t_fft_p = fc128_dwt_now();
    t_fft_q = t_fft_p; /* Packed path runs only one forward FFT */
#endif

#if FC128_PACKED_DIRECT_ZHAT
    fc128_build_zhat_from_packed_spectrum(frame_base_offset);
#else
    fc128_unpack_pq_hats_from_packed(frame_base_offset);
    fc128_compute_zhat(frame_base_offset);
#endif

#else
    fft_2d_hyperram_full(
        frame_base_offset + FC128_P_REAL,
        frame_base_offset + FC128_P_IMAG,
        frame_base_offset + FC128_P_HAT_REAL,
        frame_base_offset + FC128_P_HAT_IMAG,
        frame_base_offset + FC128_TMP_REAL,
        frame_base_offset + FC128_TMP_IMAG,
        FC_FFT_N, FC_FFT_N, false);

#if FC128_TIMING_ENABLE
    t_fft_p = fc128_dwt_now();
#endif

    fft_2d_hyperram_full(
        frame_base_offset + FC128_Q_REAL,
        frame_base_offset + FC128_Q_IMAG,
        frame_base_offset + FC128_Q_HAT_REAL,
        frame_base_offset + FC128_Q_HAT_IMAG,
        frame_base_offset + FC128_TMP_REAL,
        frame_base_offset + FC128_TMP_IMAG,
        FC_FFT_N, FC_FFT_N, false);

#if FC128_TIMING_ENABLE
    t_fft_q = fc128_dwt_now();
#endif

    fc128_compute_zhat(frame_base_offset);
#endif

#if FC128_TIMING_ENABLE
    uint32_t t_zhat = fc128_dwt_now();
#endif

    /* IFFT(Z_hat) -> Z */
    fft_2d_hyperram_full(
        frame_base_offset + FC128_Z_HAT_REAL,
        frame_base_offset + FC128_Z_HAT_IMAG,
        frame_base_offset + FC128_Z_REAL,
        frame_base_offset + FC128_Z_IMAG,
        frame_base_offset + FC128_TMP_REAL,
        frame_base_offset + FC128_TMP_IMAG,
        FC_FFT_N, FC_FFT_N, true);

#if FC128_TIMING_ENABLE
    uint32_t t_ifft = fc128_dwt_now();
#endif

#if FC128_GNG_ENABLE
    fc128_gng_update_from_frame(frame_base_offset, frame_seq);
#endif

    fc128_export_depth_u8_320x240(frame_base_offset);

#if FC128_TIMING_ENABLE
    uint32_t t_export = fc128_dwt_now();

    if ((FC128_TIMING_LOG_PERIOD != 0U) && ((frame_seq % (uint32_t)FC128_TIMING_LOG_PERIOD) == 0U))
    {
        uint32_t c_build = (uint32_t)(t_build - t0);
        uint32_t c_fft_p = (uint32_t)(t_fft_p - t_build);
        uint32_t c_fft_q = (uint32_t)(t_fft_q - t_fft_p);
        uint32_t c_zhat = (uint32_t)(t_zhat - t_fft_q);
        uint32_t c_ifft = (uint32_t)(t_ifft - t_zhat);
        uint32_t c_export = (uint32_t)(t_export - t_ifft);
        uint32_t c_total = (uint32_t)(t_export - t0);

        xprintf("[FC128] cyc build=%lu fftP=%lu fftQ=%lu zhat=%lu ifft=%lu export=%lu total=%lu\n",
                (unsigned long)c_build,
                (unsigned long)c_fft_p,
                (unsigned long)c_fft_q,
                (unsigned long)c_zhat,
                (unsigned long)c_ifft,
                (unsigned long)c_export,
                (unsigned long)c_total);

        xprintf("[FC128] us  build=%lu fftP=%lu fftQ=%lu zhat=%lu ifft=%lu export=%lu total=%lu (core=%lu Hz)\n",
                (unsigned long)fc128_cyc_to_us(c_build),
                (unsigned long)fc128_cyc_to_us(c_fft_p),
                (unsigned long)fc128_cyc_to_us(c_fft_q),
                (unsigned long)fc128_cyc_to_us(c_zhat),
                (unsigned long)fc128_cyc_to_us(c_ifft),
                (unsigned long)fc128_cyc_to_us(c_export),
                (unsigned long)fc128_cyc_to_us(c_total),
                (unsigned long)SystemCoreClock);
    }
#endif

    __DMB();
    g_depth_base_offset = frame_base_offset;
    __DMB();
    g_depth_seq = frame_seq;
}

static inline void reorder_grayscale_4px_line(uint8_t *buf, uint32_t n)
{
    /* Swap [0..1] with [2..3] within each 4px group (matches Thread1 grayscale send fix). */
    uint32_t i = 0;

#if USE_HELIUM_MVE
    /* Vectorize by treating each 4px group as one 32-bit word:
     * [a0 a1 a2 a3] -> [a2 a3 a0 a1] == rotate-by-16 within 32-bit.
     * Process 4 groups (16 bytes) per loop.
     */
    for (; (i + 3U) < n && (((uintptr_t)(&buf[i])) & 3U); i += 4U)
    {
        uint8_t a0 = buf[i];
        uint8_t a1 = buf[i + 1U];
        buf[i] = buf[i + 2U];
        buf[i + 1U] = buf[i + 3U];
        buf[i + 2U] = a0;
        buf[i + 3U] = a1;
    }

    for (; (i + 15U) < n; i += 16U)
    {
        uint32x4_t v = vld1q_u32((const uint32_t *)&buf[i]);
        uint32x4_t hi = vshrq_n_u32(v, 16);
        uint32x4_t lo = vshlq_n_u32(v, 16);
        uint32x4_t r = vorrq_u32(hi, lo);
        vst1q_u32((uint32_t *)&buf[i], r);
    }
#endif

    for (; i + 3U < n; i += 4U)
    {
        uint8_t a0 = buf[i];
        uint8_t a1 = buf[i + 1U];
        buf[i] = buf[i + 2U];
        buf[i + 1U] = buf[i + 3U];
        buf[i + 2U] = a0;
        buf[i + 3U] = a1;
    }
}

static void extract_y_line_uyvy_swap_y(const uint8_t *yuv, uint8_t *y_out, uint32_t width)
{
    /* UYVY: [U0 Y0 V0 Y1] but swap Y: output (Y1,Y0) to match current tuned grayscale view. */
    uint32_t x = 0;

#if USE_HELIUM_MVE
    /* 16 pixels = 32 bytes per iteration.
     * Input layout per 2 pixels (4 bytes): [U0 Y0 V0 Y1]
     * Output swaps Y within the pair: [Y1 Y0]
     */
    for (; (x + 15U) < width; x += 16U)
    {
        const uint8x16_t yuv_low = vld1q_u8(&yuv[x * 2U]);
        const uint8x16_t yuv_high = vld1q_u8(&yuv[x * 2U + 16U]);

        uint8_t tmp[16];
        tmp[0] = yuv_low[3];
        tmp[1] = yuv_low[1];
        tmp[2] = yuv_low[7];
        tmp[3] = yuv_low[5];
        tmp[4] = yuv_low[11];
        tmp[5] = yuv_low[9];
        tmp[6] = yuv_low[15];
        tmp[7] = yuv_low[13];
        tmp[8] = yuv_high[3];
        tmp[9] = yuv_high[1];
        tmp[10] = yuv_high[7];
        tmp[11] = yuv_high[5];
        tmp[12] = yuv_high[11];
        tmp[13] = yuv_high[9];
        tmp[14] = yuv_high[15];
        tmp[15] = yuv_high[13];

        vst1q_u8(&y_out[x], vld1q_u8(tmp));
    }
#endif

    for (; x < width; x += 2U)
    {
        uint32_t yuv_idx = x * 2U;
        y_out[x] = yuv[yuv_idx + 3U];
        y_out[x + 1U] = yuv[yuv_idx + 1U];
    }
    reorder_grayscale_4px_line(y_out, width);
}

#if PQ128_USE_CHROMA_EDGEMASK
static void extract_chroma_mag_line_uyvy_reorder(const uint8_t *yuv, uint8_t *c_out, uint32_t width)
{
    /* UYVY: [U0 Y0 V0 Y1] per 2 pixels.
     * Compute a simple chroma magnitude proxy per pixel:
     *   C = |U-128| + |V-128|  (clamped to 0..255)
     * Duplicate to both pixels of the pair, then apply the same 4px reorder
     * as the grayscale path so indexing matches y_out.
     */
    for (uint32_t x = 0; x < width; x += 2U)
    {
        uint32_t idx = x * 2U;
        int u = (int)yuv[idx + 0U];
        int v = (int)yuv[idx + 2U];
        int du = u - 128;
        int dv = v - 128;
        if (du < 0)
        {
            du = -du;
        }
        if (dv < 0)
        {
            dv = -dv;
        }
        int c = du + dv;
        if (c > 255)
        {
            c = 255;
        }
        c_out[x] = (uint8_t)c;
        if ((x + 1U) < width)
        {
            c_out[x + 1U] = (uint8_t)c;
        }
    }
    reorder_grayscale_4px_line(c_out, width);
}
#endif

static inline int clamp_i32(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/* Compute Q15 weight based on saturation proximity (1.0 .. 0.0). */
static inline int32_t pq128_sat_weight_q15(int i0, int i1)
{
    int mx = (i0 > i1) ? i0 : i1;
    if (mx >= PQ128_SAT_TH)
    {
        return 0;
    }

#if PQ128_USE_SAT_SOFTMASK
    if (mx <= PQ128_SAT_SOFT_START)
    {
        return (1 << 15);
    }
    int span = (PQ128_SAT_TH - PQ128_SAT_SOFT_START);
    if (span <= 0)
    {
        return (1 << 15);
    }
    int num = (PQ128_SAT_TH - mx);
    int32_t w = (int32_t)(((int64_t)num << 15) / (int64_t)span);
    w = clamp_i32((int)w, 0, (1 << 15));
#if PQ128_SAT_SOFT_POWER2
    /* Quadratic falloff: w <- w^2 (Q15) */
    w = (int32_t)(((int64_t)w * (int64_t)w + (1 << 14)) >> 15);
#endif
    return w;
#else
    return (1 << 15);
#endif
}

#if PQ128_USE_DARK_SOFTMASK
static inline int32_t pq128_dark_weight_q15(int i0, int i1)
{
    /* Use average intensity as a simple SNR proxy. */
    int avg = (i0 + i1) >> 1;

    if (avg <= PQ128_DARK_TH)
    {
        return 0;
    }
    if (avg >= PQ128_DARK_SOFT_END)
    {
        return (1 << 15);
    }

    int span = (PQ128_DARK_SOFT_END - PQ128_DARK_TH);
    if (span <= 0)
    {
        return 0;
    }
    int num = (avg - PQ128_DARK_TH);
    int32_t w = (int32_t)(((int64_t)num << 15) / (int64_t)span);
    w = clamp_i32((int)w, 0, (1 << 15));

#if PQ128_DARK_SOFT_POWER2
    w = (int32_t)(((int64_t)w * (int64_t)w + (1 << 14)) >> 15);
#endif
    return w;
}
#endif

#if PQ128_USE_CHROMA_EDGEMASK
static inline int32_t pq128_chroma_edge_weight_q15(int edge)
{
    if (edge <= PQ128_CHROMA_EDGE_START)
    {
        return (1 << 15);
    }
    if (edge >= PQ128_CHROMA_EDGE_TH)
    {
        return 0;
    }
    int span = (PQ128_CHROMA_EDGE_TH - PQ128_CHROMA_EDGE_START);
    if (span <= 0)
    {
        return 0;
    }
    int num = (PQ128_CHROMA_EDGE_TH - edge);
    int32_t w = (int32_t)(((int64_t)num << 15) / (int64_t)span);
    w = clamp_i32((int)w, 0, (1 << 15));
#if PQ128_CHROMA_EDGE_POWER2
    w = (int32_t)(((int64_t)w * (int64_t)w + (1 << 14)) >> 15);
#endif
    return w;
}
#endif

#if PQ128_COMPUTE_DEFOCUS_PLANES
/* Simple horizontal blur: [1 2 1]/4. Border pixels are copied. */
static inline void pq128_blur121_u8_line(const uint8_t in_u8[FRAME_WIDTH], uint8_t out_u8[FRAME_WIDTH])
{
    out_u8[0] = in_u8[0];
    out_u8[FRAME_WIDTH - 1] = in_u8[FRAME_WIDTH - 1];
    for (int x = 1; x < FRAME_WIDTH - 1; x++)
    {
        int v = (int)in_u8[x - 1] + ((int)in_u8[x] << 1) + (int)in_u8[x + 1];
        v = (v + 2) >> 2;
        out_u8[x] = (uint8_t)v;
    }
}

#endif /* PQ128_COMPUTE_DEFOCUS_PLANES */

#if PQ128_USE_FOCUS_SOFTMASK

static inline int32_t pq128_focus_weight_q15(int focus)
{
    if (focus <= PQ128_FOCUS_TH)
    {
        return 0;
    }
    if (focus >= PQ128_FOCUS_SOFT_END)
    {
        return (1 << 15);
    }

    int span = (PQ128_FOCUS_SOFT_END - PQ128_FOCUS_TH);
    if (span <= 0)
    {
        return (1 << 15);
    }
    int num = (focus - PQ128_FOCUS_TH);
    int32_t w = (int32_t)(((int64_t)num << 15) / (int64_t)span);
    w = clamp_i32((int)w, 0, (1 << 15));

#if PQ128_FOCUS_POWER2
    w = (int32_t)(((int64_t)w * (int64_t)w + (1 << 14)) >> 15);
#endif
    return w;
}
#endif

/* Forward declaration: Sobel is defined later (MVE/non-MVE variants share signature). */
static void apply_sobel_filter(uint8_t y_prev[FRAME_WIDTH],
                               uint8_t y_curr[FRAME_WIDTH],
                               uint8_t y_next[FRAME_WIDTH],
                               uint8_t edge_out[FRAME_WIDTH]);

#if (PQ128_USE_INTENSITY_KNEE || PQ128_USE_INTENSITY_GAMMA)
static void pq128_init_intensity_lut(uint8_t out_u8[256])
{
    for (int i = 0; i < 256; i++)
    {
        int y = i;

#if PQ128_USE_INTENSITY_GAMMA
        {
            float x = (float)y * (1.0f / 255.0f);
            float yg = powf(x, (float)PQ128_INTENSITY_GAMMA);
            int yi = (int)(yg * 255.0f + 0.5f);
            if (yi < 0)
            {
                yi = 0;
            }
            if (yi > 255)
            {
                yi = 255;
            }
            y = yi;
        }
#endif

#if PQ128_USE_INTENSITY_KNEE
        if (y > PQ128_KNEE_START)
        {
            y = PQ128_KNEE_START + ((y - PQ128_KNEE_START) >> PQ128_KNEE_SHIFT);
            if (y > 255)
            {
                y = 255;
            }
        }
#endif

        out_u8[i] = (uint8_t)y;
    }
}
#endif

#if (PQ128_PQ_MODE == 2)
static inline int pq128_floor_log2_u32(uint32_t x)
{
    if (x == 0U)
    {
        return 0;
    }
#if defined(__GNUC__)
    return 31 - __builtin_clz(x);
#else
    return 31 - (int)__CLZ(x);
#endif
}
#endif

#if (PQ128_PQ_MODE == 3)
static inline int16_t pq128_lightmodel_pq_i16(int intensity_u8, int32_t ratio_q15)
{
    /* p_q15 ~= (I/255) * ratio_q15 */
    int32_t pq_q15 = (int32_t)(((int64_t)intensity_u8 * (int64_t)ratio_q15 + 127) / 255);
    int32_t v = (int32_t)(((int64_t)pq_q15 * (int64_t)PQ128_LIGHTMODEL_SCALE + (1 << 14)) >> 15);
    return (int16_t)clamp_i32((int)v, -32768, 32767);
}

static void pq128_lightmodel_ratios_q15(int32_t *rp_out_q15, int32_t *rq_out_q15)
{
    float ps = g_light_ps;
    float qs = g_light_qs;
    float ts = g_light_ts;
    if (fabsf(ts) < 1.0e-6f)
    {
        ts = (ts < 0.0f) ? -1.0f : 1.0f;
    }
    float rp = ps / ts;
    float rq = qs / ts;

    int32_t rp_q15 = (int32_t)(rp * 32768.0f);
    int32_t rq_q15 = (int32_t)(rq * 32768.0f);
    /* Clamp ratios to keep p/q within sane range. */
    rp_q15 = clamp_i32((int)rp_q15, -(1 << 15), (1 << 15));
    rq_q15 = clamp_i32((int)rq_q15, -(1 << 15), (1 << 15));
    *rp_out_q15 = rp_q15;
    *rq_out_q15 = rq_q15;
}
#endif

#if PQ128_USE_TAPER && (PQ128_TAPER_WIDTH > 0)
static void pq128_init_taper_lut_q15(uint16_t out_q15[PQ128_SIZE])
{
    /* Raised-cosine ramp from 0 at the very edge to 1 at distance >= PQ128_TAPER_WIDTH. */
    const int W = PQ128_TAPER_WIDTH;
    const float pi = 3.14159265358979323846f;

    for (int i = 0; i < PQ128_SIZE; i++)
    {
        int d = i;
        int d2 = (PQ128_SIZE - 1) - i;
        if (d2 < d)
        {
            d = d2;
        }

        if (d >= W)
        {
            out_q15[i] = (uint16_t)(1U << 15);
            continue;
        }

        float t = (float)d / (float)W; /* [0..1) */
        float w = 0.5f - 0.5f * cosf(pi * t);
        int32_t q15 = (int32_t)(w * 32768.0f + 0.5f);
        if (q15 < 0)
        {
            q15 = 0;
        }
        if (q15 > (1 << 15))
        {
            q15 = (1 << 15);
        }
        out_q15[i] = (uint16_t)q15;
    }
}
#endif

static fsp_err_t load_y_line_from_hyperram_base(uint32_t frame_base_offset,
                                                int requested_row,
                                                uint8_t yuv_line[FRAME_WIDTH * 2],
                                                uint8_t y_line[FRAME_WIDTH])
{
    int row = clamp_i32(requested_row, 0, FRAME_HEIGHT - 1);
    uint32_t offset = frame_base_offset + (uint32_t)row * (uint32_t)FRAME_WIDTH * 2U;
    fsp_err_t err = hyperram_b_read(yuv_line, (void *)offset, FRAME_WIDTH * 2);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    extract_y_line_uyvy_swap_y(yuv_line, y_line, (uint32_t)FRAME_WIDTH);
    return FSP_SUCCESS;
}

#if PQ128_USE_CHROMA_EDGEMASK
static fsp_err_t load_yc_line_from_hyperram_base(uint32_t frame_base_offset,
                                                 int requested_row,
                                                 uint8_t yuv_line[FRAME_WIDTH * 2],
                                                 uint8_t y_line[FRAME_WIDTH],
                                                 uint8_t c_line[FRAME_WIDTH])
{
    int row = clamp_i32(requested_row, 0, FRAME_HEIGHT - 1);
    uint32_t offset = frame_base_offset + (uint32_t)row * (uint32_t)FRAME_WIDTH * 2U;
    fsp_err_t err = hyperram_b_read(yuv_line, (void *)offset, FRAME_WIDTH * 2);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    extract_y_line_uyvy_swap_y(yuv_line, y_line, (uint32_t)FRAME_WIDTH);
    extract_chroma_mag_line_uyvy_reorder(yuv_line, c_line, (uint32_t)FRAME_WIDTH);
    return FSP_SUCCESS;
}

static void load_yc_line_from_hyperram_or_zero(uint32_t frame_base_offset,
                                               int requested_row,
                                               uint8_t yuv_line[FRAME_WIDTH * 2],
                                               uint8_t y_line[FRAME_WIDTH],
                                               uint8_t c_line[FRAME_WIDTH])
{
    if ((requested_row < 0) || (requested_row >= FRAME_HEIGHT))
    {
        memset(y_line, 0, FRAME_WIDTH);
        memset(c_line, 0, FRAME_WIDTH);
        return;
    }
    (void)load_yc_line_from_hyperram_base(frame_base_offset, requested_row, yuv_line, y_line, c_line);
}

static inline uint8_t pq128_get_c_or_zero(const uint8_t c_line[FRAME_WIDTH], int x)
{
    if ((x < 0) || (x >= FRAME_WIDTH))
    {
        return 0;
    }
    return c_line[x];
}
#endif

static FC128_UNUSED void load_y_line_from_hyperram_or_zero(uint32_t frame_base_offset,
                                                           int requested_row,
                                                           uint8_t yuv_line[FRAME_WIDTH * 2],
                                                           uint8_t y_line[FRAME_WIDTH])
{
    if ((requested_row < 0) || (requested_row >= FRAME_HEIGHT))
    {
        memset(y_line, 0, FRAME_WIDTH);
        return;
    }
    (void)load_y_line_from_hyperram_base(frame_base_offset, requested_row, yuv_line, y_line);
}

static inline uint8_t pq128_get_y_or_zero(const uint8_t y_line[FRAME_WIDTH], int x)
{
    if ((x < 0) || (x >= FRAME_WIDTH))
    {
        return 0;
    }
    return y_line[x];
}

static void pq128_compute_and_store(uint32_t frame_base_offset, uint32_t frame_seq)
{
    uint8_t yuv_tmp[FRAME_WIDTH * 2];
    uint8_t y_prev[FRAME_WIDTH];
    uint8_t y_curr[FRAME_WIDTH];
    uint8_t y_next[FRAME_WIDTH];
#if PQ128_USE_FOCUS_SOFTMASK
    uint8_t y_prev_blur[FRAME_WIDTH];
    uint8_t y_curr_blur[FRAME_WIDTH];
    uint8_t y_next_blur[FRAME_WIDTH];
    uint8_t y_blur_tmp[FRAME_WIDTH];
    uint8_t edge_orig[FRAME_WIDTH];
    uint8_t edge_blur[FRAME_WIDTH];
    uint8_t focus_line[FRAME_WIDTH];
#if PQ128_STORE_FOCUS_PLANE
    uint8_t focus_row[PQ128_SIZE];
#endif
#if PQ128_STORE_EDGE_PLANE
    uint8_t edge_row[PQ128_SIZE];
#endif
#if PQ128_STORE_EDGE_BLUR_PLANE
    uint8_t edge_blur_row[PQ128_SIZE];
#endif
#if PQ128_STORE_SATMASK_PLANE
    uint8_t satmask_row[PQ128_SIZE];
#endif
#endif
#if PQ128_USE_CHROMA_EDGEMASK
    uint8_t c_prev[FRAME_WIDTH];
    uint8_t c_curr[FRAME_WIDTH];
    uint8_t c_next[FRAME_WIDTH];
#endif
    int16_t p_row[PQ128_SIZE];
    int16_t q_row[PQ128_SIZE];

    /* Mark as in-progress (consumer should wait for a non-zero stable seq). */
    g_pq128_seq = 0;

    /* Prime 3-line window.
     * Sliding-window fast path is valid only when stride_y==1.
     */
#if (PQ128_SAMPLE_STRIDE_Y == 1)
#if PQ128_USE_CHROMA_EDGEMASK
    load_yc_line_from_hyperram_or_zero(frame_base_offset, PQ128_Y0 - 1, yuv_tmp, y_prev, c_prev);
    load_yc_line_from_hyperram_or_zero(frame_base_offset, PQ128_Y0 + 0, yuv_tmp, y_curr, c_curr);
    load_yc_line_from_hyperram_or_zero(frame_base_offset, PQ128_Y0 + 1, yuv_tmp, y_next, c_next);
#else
    load_y_line_from_hyperram_or_zero(frame_base_offset, PQ128_Y0 - 1, yuv_tmp, y_prev);
    load_y_line_from_hyperram_or_zero(frame_base_offset, PQ128_Y0 + 0, yuv_tmp, y_curr);
    load_y_line_from_hyperram_or_zero(frame_base_offset, PQ128_Y0 + 1, yuv_tmp, y_next);
#endif
#endif

    /*
     * p,q generation (selected by PQ128_PQ_MODE).
     * Default is contrast-invariant (reduces albedo/brightness bias):
     *   p = (I(x+1)-I(x-1)) / (I(x+1)+I(x-1)+eps)
     *   q = (I(y+1)-I(y-1)) / (I(y+1)+I(y-1)+eps)
     * Strong-approx modes can be used for extra speed.
     */

#if (PQ128_PQ_MODE == 0) && PQ128_USE_RECIP_LUT
    /* Q15 reciprocal LUT for v = (num * PQ128_NORM_SCALE) / den.
     * recip_q15[den] = (PQ128_NORM_SCALE<<15)/den
     */
    static bool s_recip_inited = false;
    static uint32_t s_recip_q15[PQ128_DEN_MAX + 1];
    if (!s_recip_inited)
    {
        s_recip_q15[0] = 0U;
        for (int d = 1; d <= (int)PQ128_DEN_MAX; d++)
        {
            s_recip_q15[d] = (((uint32_t)PQ128_NORM_SCALE) << 15) / (uint32_t)d;
        }
        s_recip_inited = true;
    }
#endif

#if (PQ128_USE_INTENSITY_KNEE || PQ128_USE_INTENSITY_GAMMA)
    static bool s_intensity_lut_inited = false;
    static uint8_t s_intensity_u8[256];
    if (!s_intensity_lut_inited)
    {
        pq128_init_intensity_lut(s_intensity_u8);
        s_intensity_lut_inited = true;
    }
#endif

#if PQ128_USE_TAPER && (PQ128_TAPER_WIDTH > 0)
    static bool s_taper_inited = false;
    static uint16_t s_taper_q15[PQ128_SIZE];
    if (!s_taper_inited)
    {
        pq128_init_taper_lut_q15(s_taper_q15);
        s_taper_inited = true;
    }
#endif

#if (PQ128_PQ_MODE == 3)
    int32_t rp_q15 = 0;
    int32_t rq_q15 = 0;
    pq128_lightmodel_ratios_q15(&rp_q15, &rq_q15);
#endif

    for (int ry = 0; ry < PQ128_SIZE; ry++)
    {
#if (PQ128_SAMPLE_STRIDE_Y != 1)
        int src_y = PQ128_Y0 + ry * PQ128_SAMPLE_STRIDE_Y;
#if PQ128_USE_CHROMA_EDGEMASK
        load_yc_line_from_hyperram_or_zero(frame_base_offset, src_y - PQ128_SAMPLE_STRIDE_Y, yuv_tmp, y_prev, c_prev);
        load_yc_line_from_hyperram_or_zero(frame_base_offset, src_y, yuv_tmp, y_curr, c_curr);
        load_yc_line_from_hyperram_or_zero(frame_base_offset, src_y + PQ128_SAMPLE_STRIDE_Y, yuv_tmp, y_next, c_next);
#else
        load_y_line_from_hyperram_or_zero(frame_base_offset, src_y - PQ128_SAMPLE_STRIDE_Y, yuv_tmp, y_prev);
        load_y_line_from_hyperram_or_zero(frame_base_offset, src_y, yuv_tmp, y_curr);
        load_y_line_from_hyperram_or_zero(frame_base_offset, src_y + PQ128_SAMPLE_STRIDE_Y, yuv_tmp, y_next);
#endif
#endif

#if PQ128_USE_FOCUS_SOFTMASK
        /* Focus cue: edge(original) - edge(blurred). */
        pq128_blur121_u8_line(y_prev, y_prev_blur);
        pq128_blur121_u8_line(y_curr, y_curr_blur);
        pq128_blur121_u8_line(y_next, y_next_blur);

#if (PQ128_FOCUS_BLUR_PASSES > 1)
        for (int pass = 1; pass < PQ128_FOCUS_BLUR_PASSES; pass++)
        {
            pq128_blur121_u8_line(y_prev_blur, y_blur_tmp);
            memcpy(y_prev_blur, y_blur_tmp, FRAME_WIDTH);

            pq128_blur121_u8_line(y_curr_blur, y_blur_tmp);
            memcpy(y_curr_blur, y_blur_tmp, FRAME_WIDTH);

            pq128_blur121_u8_line(y_next_blur, y_blur_tmp);
            memcpy(y_next_blur, y_blur_tmp, FRAME_WIDTH);
        }
#endif

        apply_sobel_filter(y_prev, y_curr, y_next, edge_orig);
        apply_sobel_filter(y_prev_blur, y_curr_blur, y_next_blur, edge_blur);

        for (int x0 = 0; x0 < FRAME_WIDTH; x0++)
        {
            int d = (int)edge_orig[x0] - (int)edge_blur[x0];
            focus_line[x0] = (uint8_t)((d > 0) ? d : 0);
        }
#endif

#if PQ128_USE_TAPER && (PQ128_TAPER_WIDTH > 0)
        int32_t wy_q15 = (int32_t)s_taper_q15[ry];
#endif
        /* Compute p,q within ROI. Outer border is set to 0 (see PQ128_BORDER_ZERO_N). */
        for (int rx = 0; rx < PQ128_SIZE; rx++)
        {
            if ((ry < PQ128_BORDER_ZERO_N) || (ry >= (PQ128_SIZE - PQ128_BORDER_ZERO_N)) ||
                (rx < PQ128_BORDER_ZERO_N) || (rx >= (PQ128_SIZE - PQ128_BORDER_ZERO_N)))
            {
                p_row[rx] = 0;
                q_row[rx] = 0;
#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_FOCUS_PLANE
                focus_row[rx] = 0;
#endif
#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_EDGE_PLANE
                edge_row[rx] = 0;
#endif
#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_EDGE_BLUR_PLANE
                edge_blur_row[rx] = 0;
#endif
#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_SATMASK_PLANE
                satmask_row[rx] = 0;
#endif
                continue;
            }

            int x = PQ128_X0 + rx * PQ128_SAMPLE_STRIDE_X;

#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_FOCUS_PLANE
            focus_row[rx] = pq128_get_y_or_zero(focus_line, x);
#endif
#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_EDGE_PLANE
            edge_row[rx] = pq128_get_y_or_zero(edge_orig, x);
#endif
#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_EDGE_BLUR_PLANE
            edge_blur_row[rx] = pq128_get_y_or_zero(edge_blur, x);
#endif

#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_SATMASK_PLANE
            {
                const int raw_c = (int)pq128_get_y_or_zero(y_curr, x);
                const int raw_xm1_m = (int)pq128_get_y_or_zero(y_curr, x - PQ128_SAMPLE_STRIDE_X);
                const int raw_xp1_m = (int)pq128_get_y_or_zero(y_curr, x + PQ128_SAMPLE_STRIDE_X);
                const int raw_ym1_m = (int)pq128_get_y_or_zero(y_prev, x);
                const int raw_yp1_m = (int)pq128_get_y_or_zero(y_next, x);

                const int hi = (int)PQ128_SATMASK_HI_TH;
                const int lo = (int)PQ128_SATMASK_LO_TH;

                const bool sat = (raw_c >= hi) || (raw_xm1_m >= hi) || (raw_xp1_m >= hi) || (raw_ym1_m >= hi) || (raw_yp1_m >= hi);
                const bool dark = (raw_c <= lo) || (raw_xm1_m <= lo) || (raw_xp1_m <= lo) || (raw_ym1_m <= lo) || (raw_yp1_m <= lo);

                satmask_row[rx] = (uint8_t)((!sat && !dark) ? 255 : 0);
            }
#endif
            int raw_xm1 = (int)pq128_get_y_or_zero(y_curr, x - PQ128_SAMPLE_STRIDE_X);
            int raw_xp1 = (int)pq128_get_y_or_zero(y_curr, x + PQ128_SAMPLE_STRIDE_X);
            int raw_ym1 = (int)pq128_get_y_or_zero(y_prev, x);
            int raw_yp1 = (int)pq128_get_y_or_zero(y_next, x);

#if (PQ128_USE_INTENSITY_KNEE || PQ128_USE_INTENSITY_GAMMA)
            int i_xm1 = (int)s_intensity_u8[(uint8_t)raw_xm1];
            int i_xp1 = (int)s_intensity_u8[(uint8_t)raw_xp1];
            int i_ym1 = (int)s_intensity_u8[(uint8_t)raw_ym1];
            int i_yp1 = (int)s_intensity_u8[(uint8_t)raw_yp1];
#else
            int i_xm1 = raw_xm1;
            int i_xp1 = raw_xp1;
            int i_ym1 = raw_ym1;
            int i_yp1 = raw_yp1;
#endif

            /* Saturation detection should use raw intensity (before knee). */
            int32_t w_p_q15 = pq128_sat_weight_q15(raw_xm1, raw_xp1);
            int32_t w_q_q15 = pq128_sat_weight_q15(raw_ym1, raw_yp1);

#if PQ128_USE_DARK_SOFTMASK
            {
                int32_t wd_p_q15 = pq128_dark_weight_q15(raw_xm1, raw_xp1);
                int32_t wd_q_q15 = pq128_dark_weight_q15(raw_ym1, raw_yp1);
                w_p_q15 = (int32_t)(((int64_t)w_p_q15 * (int64_t)wd_p_q15 + (1 << 14)) >> 15);
                w_q_q15 = (int32_t)(((int64_t)w_q_q15 * (int64_t)wd_q_q15 + (1 << 14)) >> 15);
            }
#endif

#if PQ128_USE_CHROMA_EDGEMASK
            {
                int c_xm1 = (int)pq128_get_c_or_zero(c_curr, x - PQ128_SAMPLE_STRIDE_X);
                int c_xp1 = (int)pq128_get_c_or_zero(c_curr, x + PQ128_SAMPLE_STRIDE_X);
                int c_ym1 = (int)pq128_get_c_or_zero(c_prev, x);
                int c_yp1 = (int)pq128_get_c_or_zero(c_next, x);

                int dc_p = c_xp1 - c_xm1;
                if (dc_p < 0)
                {
                    dc_p = -dc_p;
                }
                int dc_q = c_yp1 - c_ym1;
                if (dc_q < 0)
                {
                    dc_q = -dc_q;
                }

                int32_t w_cp_q15 = pq128_chroma_edge_weight_q15(dc_p);
                int32_t w_cq_q15 = pq128_chroma_edge_weight_q15(dc_q);
                w_p_q15 = (int32_t)(((int64_t)w_p_q15 * (int64_t)w_cp_q15 + (1 << 14)) >> 15);
                w_q_q15 = (int32_t)(((int64_t)w_q_q15 * (int64_t)w_cq_q15 + (1 << 14)) >> 15);
            }
#endif

#if PQ128_USE_TAPER && (PQ128_TAPER_WIDTH > 0)
            int32_t wx_q15 = (int32_t)s_taper_q15[rx];
            int32_t w_taper_q15 = (int32_t)(((int64_t)wx_q15 * (int64_t)wy_q15 + (1 << 14)) >> 15);
            w_p_q15 = (int32_t)(((int64_t)w_p_q15 * (int64_t)w_taper_q15 + (1 << 14)) >> 15);
            w_q_q15 = (int32_t)(((int64_t)w_q_q15 * (int64_t)w_taper_q15 + (1 << 14)) >> 15);
#endif

#if PQ128_USE_FOCUS_SOFTMASK
            {
                int focus = (int)pq128_get_y_or_zero(focus_line, x);
                int32_t w_f_q15 = pq128_focus_weight_q15(focus);
                w_p_q15 = (int32_t)(((int64_t)w_p_q15 * (int64_t)w_f_q15 + (1 << 14)) >> 15);
                w_q_q15 = (int32_t)(((int64_t)w_q_q15 * (int64_t)w_f_q15 + (1 << 14)) >> 15);
            }
#endif

            if (w_p_q15 == 0)
            {
                p_row[rx] = 0;
            }
            else
            {
#if (PQ128_PQ_MODE == 3)
                int raw_c = (int)pq128_get_y_or_zero(y_curr, x);
#if (PQ128_USE_INTENSITY_KNEE || PQ128_USE_INTENSITY_GAMMA)
                int i_c = (int)s_intensity_u8[(uint8_t)raw_c];
#else
                int i_c = raw_c;
#endif
                int32_t v = (int32_t)pq128_lightmodel_pq_i16(i_c, rp_q15);
#else
                int num = i_xp1 - i_xm1;
                int den = i_xp1 + i_xm1 + PQ128_NORM_EPS;
                if (den < 0)
                {
                    den = 0;
                }
                if (den > (int)PQ128_DEN_MAX)
                {
                    den = (int)PQ128_DEN_MAX;
                }

                int32_t v;
#if (PQ128_PQ_MODE == 1)
                (void)den;
                v = (int32_t)((int32_t)num * (int32_t)PQ128_DIFF_SCALE);
#elif (PQ128_PQ_MODE == 2)
                if (den <= 0)
                {
                    v = 0;
                }
                else
                {
                    int sh = pq128_floor_log2_u32((uint32_t)den);
                    v = (int32_t)(((int64_t)num * (int64_t)PQ128_NORM_SCALE) >> sh);
                }
#else
#if PQ128_USE_RECIP_LUT
                uint32_t recip = s_recip_q15[den];
                v = (int32_t)(((int64_t)num * (int64_t)recip + (1 << 14)) >> 15);
#else
                v = (int32_t)((num * PQ128_NORM_SCALE) / den);
#endif
#endif
#endif

#if PQ128_APPLY_STRIDE_COMPENSATION
                if (PQ128_SAMPLE_STRIDE_X > 1)
                {
                    v /= PQ128_SAMPLE_STRIDE_X;
                }
#endif

#if PQ128_FLIP_P
                v = -v;
#endif

                /* Apply saturation attenuation weight. */
                v = (int32_t)(((int64_t)v * (int64_t)w_p_q15 + (1 << 14)) >> 15);
                p_row[rx] = (int16_t)clamp_i32((int)v, -32768, 32767);
            }

            if (w_q_q15 == 0)
            {
                q_row[rx] = 0;
            }
            else
            {
#if (PQ128_PQ_MODE == 3)
                int raw_c = (int)pq128_get_y_or_zero(y_curr, x);
#if PQ128_USE_INTENSITY_KNEE
                int i_c = (int)s_knee_u8[(uint8_t)raw_c];
#else
                int i_c = raw_c;
#endif
                int32_t v = (int32_t)pq128_lightmodel_pq_i16(i_c, rq_q15);
#else
                int num = i_yp1 - i_ym1;
                int den = i_yp1 + i_ym1 + PQ128_NORM_EPS;
                if (den < 0)
                {
                    den = 0;
                }
                if (den > (int)PQ128_DEN_MAX)
                {
                    den = (int)PQ128_DEN_MAX;
                }

                int32_t v;
#if (PQ128_PQ_MODE == 1)
                (void)den;
                v = (int32_t)((int32_t)num * (int32_t)PQ128_DIFF_SCALE);
#elif (PQ128_PQ_MODE == 2)
                if (den <= 0)
                {
                    v = 0;
                }
                else
                {
                    int sh = pq128_floor_log2_u32((uint32_t)den);
                    v = (int32_t)(((int64_t)num * (int64_t)PQ128_NORM_SCALE) >> sh);
                }
#else
#if PQ128_USE_RECIP_LUT
                uint32_t recip = s_recip_q15[den];
                v = (int32_t)(((int64_t)num * (int64_t)recip + (1 << 14)) >> 15);
#else
                v = (int32_t)((num * PQ128_NORM_SCALE) / den);
#endif
#endif
#endif

#if PQ128_APPLY_STRIDE_COMPENSATION
                if (PQ128_SAMPLE_STRIDE_Y > 1)
                {
                    v /= PQ128_SAMPLE_STRIDE_Y;
                }
#endif

#if PQ128_FLIP_Q
                v = -v;
#endif

                /* Apply saturation attenuation weight. */
                v = (int32_t)(((int64_t)v * (int64_t)w_q_q15 + (1 << 14)) >> 15);
                q_row[rx] = (int16_t)clamp_i32((int)v, -32768, 32767);
            }
        }

        uint32_t row_off = (uint32_t)ry * (uint32_t)PQ128_SIZE * (uint32_t)sizeof(int16_t);
        (void)hyperram_b_write(p_row, (void *)(frame_base_offset + PQ128_P_OFFSET + row_off), (uint32_t)PQ128_SIZE * (uint32_t)sizeof(int16_t));
        (void)hyperram_b_write(q_row, (void *)(frame_base_offset + PQ128_Q_OFFSET + row_off), (uint32_t)PQ128_SIZE * (uint32_t)sizeof(int16_t));

#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_FOCUS_PLANE
        (void)hyperram_b_write(focus_row,
                               (void *)(frame_base_offset + PQ128_FOCUS_OFFSET + (uint32_t)ry * (uint32_t)PQ128_SIZE),
                               (uint32_t)PQ128_SIZE);
#endif

#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_EDGE_PLANE
        (void)hyperram_b_write(edge_row,
                               (void *)(frame_base_offset + PQ128_EDGE_OFFSET + (uint32_t)ry * (uint32_t)PQ128_SIZE),
                               (uint32_t)PQ128_SIZE);
#endif

#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_EDGE_BLUR_PLANE
        (void)hyperram_b_write(edge_blur_row,
                               (void *)(frame_base_offset + PQ128_EDGE_BLUR_OFFSET + (uint32_t)ry * (uint32_t)PQ128_SIZE),
                               (uint32_t)PQ128_SIZE);
#endif

#if PQ128_USE_FOCUS_SOFTMASK && PQ128_STORE_SATMASK_PLANE
        (void)hyperram_b_write(satmask_row,
                               (void *)(frame_base_offset + PQ128_SATMASK_OFFSET + (uint32_t)ry * (uint32_t)PQ128_SIZE),
                               (uint32_t)PQ128_SIZE);
#endif

#if (PQ128_SAMPLE_STRIDE_Y == 1)
        /* Slide window: prev <- curr, curr <- next, next <- load(row+2). */
        memcpy(y_prev, y_curr, FRAME_WIDTH);
        memcpy(y_curr, y_next, FRAME_WIDTH);
#if PQ128_USE_CHROMA_EDGEMASK
        memcpy(c_prev, c_curr, FRAME_WIDTH);
        memcpy(c_curr, c_next, FRAME_WIDTH);
        load_yc_line_from_hyperram_or_zero(frame_base_offset, PQ128_Y0 + ry + 2, yuv_tmp, y_next, c_next);
#else
        load_y_line_from_hyperram_or_zero(frame_base_offset, PQ128_Y0 + ry + 2, yuv_tmp, y_next);
#endif
#endif
    }

    __DMB();
    g_pq128_base_offset = frame_base_offset;
    __DMB();
    g_pq128_seq = frame_seq;
}

#if USE_DEPTH_METHOD == 1
#define MG_WORK_OFFSET (DEPTH_OFFSET + FRAME_WIDTH * FRAME_HEIGHT)
#define MG_MAX_LEVELS 6

typedef struct
{
    uint32_t z_offset;
    uint32_t rhs_offset;
    uint32_t residual_offset; // 各レベル専用の残差バッファ
    int width;
    int height;
} mg_level_t;

static mg_level_t g_mg_levels[MG_MAX_LEVELS];
static int g_mg_level_count = 0;
static bool g_mg_layout_ready = false;
#endif

// Shape from Shading 光源パラメータ（変数）
static float g_light_ps = 0.0f; // 光源方向x成分
static float g_light_qs = 0.0f; // 光源方向y成分
static float g_light_ts = 1.0f; // 光源方向z成分（正規化された垂直光源）

/* Force light source to be straight above (0,0,1). */
#ifndef FIX_LIGHT_SOURCE_OVERHEAD
#define FIX_LIGHT_SOURCE_OVERHEAD (1)
#endif

/* NOTE:
 * PQ128_PQ_MODE==3 (light-model overwrite) requires a non-overhead light direction.
 * If FIX_LIGHT_SOURCE_OVERHEAD==1 then ps=qs=0 and p=q~=0, which tends to produce a flat
 * (nearly constant) depth map (often appears all-blue with fixed [0..255] heatmap scaling).
 */
#if (PQ128_PQ_MODE == 3) && (FIX_LIGHT_SOURCE_OVERHEAD)
#warning "PQ128_PQ_MODE=3 with FIX_LIGHT_SOURCE_OVERHEAD=1 will degenerate (p=q~=0). Disable overhead forcing or choose PQ128_PQ_MODE=0/1/2."
#endif

/* 光源パラメータを更新する関数
 * センサーからの入力または固定値を設定
 * 注意: 光源ベクトルは正規化されている必要があります
 *       sqrt(ps^2 + qs^2 + ts^2) = 1
 */
void update_light_source(float ps, float qs, float ts)
{
#if FIX_LIGHT_SOURCE_OVERHEAD
    (void)ps;
    (void)qs;
    (void)ts;
    g_light_ps = 0.0f;
    g_light_qs = 0.0f;
    g_light_ts = 1.0f;
#else
    // 正規化
    float magnitude = sqrtf(ps * ps + qs * qs + ts * ts);
    if (magnitude > 1e-6f)
    {
        g_light_ps = ps / magnitude;
        g_light_qs = qs / magnitude;
        g_light_ts = ts / magnitude;
    }
    else
    {
        // デフォルト値（垂直光源）
        g_light_ps = 0.0f;
        g_light_qs = 0.0f;
        g_light_ts = 1.0f;
    }
#endif
}

/* 光源パラメータを取得する関数 */
void get_light_source(float *ps, float *qs, float *ts)
{
    if (ps)
        *ps = g_light_ps;
    if (qs)
        *qs = g_light_qs;
    if (ts)
        *ts = g_light_ts;
}

/* 輝度を正規化してコントラストを一定にする
 * 薄暗い環境でも明るい環境でも同じ深度推定結果を得る
 */
#if USE_BRIGHTNESS_NORMALIZATION
static void normalize_brightness(uint8_t *y_line, int width)
{
    // フェーズ1: 最小値と最大値を検索
    uint8_t min_val = 255;
    uint8_t max_val = 0;

#if USE_HELIUM_MVE
    // MVE版: 16要素単位でmin/max検索
    int x;
    for (x = 0; x < width - 15; x += 16)
    {
        uint8x16_t vec = vld1q_u8(&y_line[x]);

        // ベクトル内の各要素をチェック（リダクション命令がないため）
        uint8_t temp[16];
        vst1q_u8(temp, vec);
        for (int i = 0; i < 16; i++)
        {
            if (temp[i] < min_val)
                min_val = temp[i];
            if (temp[i] > max_val)
                max_val = temp[i];
        }
    }

    // 残りをスカラー処理
    for (; x < width; x++)
    {
        if (y_line[x] < min_val)
            min_val = y_line[x];
        if (y_line[x] > max_val)
            max_val = y_line[x];
    }
#else
    // スカラー版
    for (int x = 0; x < width; x++)
    {
        if (y_line[x] < min_val)
            min_val = y_line[x];
        if (y_line[x] > max_val)
            max_val = y_line[x];
    }
#endif

    // コントラストが低すぎる場合はスキップ（ノイズ対策）
    int range = max_val - min_val;
    if (range < 20)
    {
        return;
    }

    // フェーズ2: 0-255の範囲に伸長（ヒストグラム伸長）
    float scale = 255.0f / (float)range;

#if USE_HELIUM_MVE
    // MVE版: 真のベクトル処理（16要素単位）
    // 固定小数点スケール: scale_fixed = scale * 256
    uint32_t scale_fixed = (uint32_t)(scale * 256.0f + 0.5f);
    uint8x16_t min_vec = vdupq_n_u8(min_val);

    for (x = 0; x < width - 15; x += 16)
    {
        // 16バイトロード
        uint8x16_t data = vld1q_u8(&y_line[x]);

        // min_val減算（飽和減算）
        uint8x16_t adjusted = vqsubq_u8(data, min_vec);

        // 下位8バイトを16ビットに拡張
        uint16x8_t low = vmovlbq_u8(adjusted);
        // スケール乗算（固定小数点）
        low = vmulq_n_u16(low, (uint16_t)scale_fixed);
        // 8ビット右シフトで÷256（飽和シフト付き狭窄でuint8に戻す）
        uint8x16_t result = vqshrnbq_n_u16(vuninitializedq_u8(), low, 8);

        // 上位8バイトを16ビットに拡張
        uint16x8_t high = vmovltq_u8(adjusted);
        high = vmulq_n_u16(high, (uint16_t)scale_fixed);
        // 8ビット右シフト + 上位8バイトに狭窄格納
        result = vqshrntq_n_u16(result, high, 8);

        // 結果を保存
        vst1q_u8(&y_line[x], result);
    }

    // 残りをスカラー処理
    for (; x < width; x++)
    {
        int normalized = (int)((y_line[x] - min_val) * scale);
        if (normalized < 0)
            normalized = 0;
        if (normalized > 255)
            normalized = 255;
        y_line[x] = (uint8_t)normalized;
    }
#else
    // スカラー版
    for (int x = 0; x < width; x++)
    {
        int normalized = (int)((y_line[x] - min_val) * scale);
        if (normalized < 0)
            normalized = 0;
        if (normalized > 255)
            normalized = 255;
        y_line[x] = (uint8_t)normalized;
    }
#endif
}
#endif

/* YUV422からY成分（輝度）を抽出
 * YUV422フォーマット: [V0 Y1 U0 Y0] (リトルエンディアン)
 */
#if USE_HELIUM_MVE
static void extract_y_component(uint8_t *yuv_line, uint8_t *y_line, int width)
{
    // MVEベクトル化: 16ピクセル（32バイト）単位で処理
    // YUV422: [V0 Y1 U0 Y0] [V1 Y3 U1 Y2] (4バイト=2ピクセル)
    // Y成分位置: byte[3]=Y0, byte[1]=Y1, byte[7]=Y2, byte[5]=Y3, ...

    int x;
    for (x = 0; x < width - 15; x += 16)
    {
        // 32バイト読み込み（16ピクセル分のYUV422データ）
        uint8x16_t yuv_low = vld1q_u8(&yuv_line[x * 2]);
        uint8x16_t yuv_high = vld1q_u8(&yuv_line[x * 2 + 16]);

        // Y成分を抽出（MVE効率的処理）
        // Y0,Y1,Y2,Y3,...,Y15の順に並べる
        uint8_t temp[16];
        temp[0] = yuv_low[3];    // Y0
        temp[1] = yuv_low[1];    // Y1
        temp[2] = yuv_low[7];    // Y2
        temp[3] = yuv_low[5];    // Y3
        temp[4] = yuv_low[11];   // Y4
        temp[5] = yuv_low[9];    // Y5
        temp[6] = yuv_low[15];   // Y6
        temp[7] = yuv_low[13];   // Y7
        temp[8] = yuv_high[3];   // Y8
        temp[9] = yuv_high[1];   // Y9
        temp[10] = yuv_high[7];  // Y10
        temp[11] = yuv_high[5];  // Y11
        temp[12] = yuv_high[11]; // Y12
        temp[13] = yuv_high[9];  // Y13
        temp[14] = yuv_high[15]; // Y14
        temp[15] = yuv_high[13]; // Y15

        // MVEベクトルロード/ストアで高速化
        uint8x16_t y_result = vld1q_u8(temp);
        vst1q_u8(&y_line[x], y_result);
    }

    // 残りをスカラー処理
    for (; x < width; x += 2)
    {
        int yuv_index = x * 2;
        y_line[x] = yuv_line[yuv_index + 3];     // Y0
        y_line[x + 1] = yuv_line[yuv_index + 1]; // Y1
    }
}
#else
static void extract_y_component(uint8_t *yuv_line, uint8_t *y_line, int width)
{
    for (int x = 0; x < width; x += 2)
    {
        int yuv_index = x * 2;                   // 2ピクセル = 4バイト
        y_line[x] = yuv_line[yuv_index + 3];     // Y0
        y_line[x + 1] = yuv_line[yuv_index + 1]; // Y1
    }
}
#endif

// HyperRAMアクセスを最小限にするための行キャッシュ補助関数
static inline int clamp_frame_row(int row)
{
    if (row < 0)
    {
        return 0;
    }
    if (row >= FRAME_HEIGHT)
    {
        return FRAME_HEIGHT - 1;
    }
    return row;
}

static FC128_UNUSED fsp_err_t load_y_line_from_hyperram(int requested_row,
                                                        uint8_t yuv_line[FRAME_WIDTH * 2],
                                                        uint8_t y_line[FRAME_WIDTH],
                                                        int *loaded_row_out)
{
    int row = clamp_frame_row(requested_row);
    if (loaded_row_out)
    {
        *loaded_row_out = row;
    }

    uint32_t offset = (uint32_t)row * FRAME_WIDTH * 2;
    fsp_err_t err = hyperram_b_read(yuv_line, (void *)offset, FRAME_WIDTH * 2);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    extract_y_component(yuv_line, y_line, FRAME_WIDTH);

#if USE_BRIGHTNESS_NORMALIZATION
    // 輝度を正規化（明るさに依存しない深度推定）
    normalize_brightness(y_line, FRAME_WIDTH);
#endif

    return FSP_SUCCESS;
}

static FC128_UNUSED void duplicate_line_buffer(uint8_t dst_yuv[FRAME_WIDTH * 2],
                                               uint8_t dst_y[FRAME_WIDTH],
                                               const uint8_t src_yuv[FRAME_WIDTH * 2],
                                               const uint8_t src_y[FRAME_WIDTH])
{
    memcpy(dst_yuv, src_yuv, FRAME_WIDTH * 2);
    memcpy(dst_y, src_y, FRAME_WIDTH);
}

/* Sobelフィルタでエッジ検出
 * 入力: 3行分のY成分（前の行、現在の行、次の行）
 * 出力: エッジ強度（0-255）
 */

#if USE_HELIUM_MVE
// Helium MVE版 - シンプルで安全な実装（スカラー計算 + ベクトル後処理）
static FC128_UNUSED void apply_sobel_filter(uint8_t y_prev[FRAME_WIDTH],
                                            uint8_t y_curr[FRAME_WIDTH],
                                            uint8_t y_next[FRAME_WIDTH],
                                            uint8_t edge_out[FRAME_WIDTH])
{
    // 境界ピクセルは中央ピクセルの値をそのまま使う
    edge_out[0] = y_curr[0];
    edge_out[FRAME_WIDTH - 1] = y_curr[FRAME_WIDTH - 1];

    // 8ピクセル単位で処理（スカラー計算 + MVEベクトル後処理）
    int x;
    for (x = 1; x < FRAME_WIDTH - 8; x += 8)
    {
        // 8ピクセル分のSobel演算をスカラーで計算
        int16_t gx_buf[8] __attribute__((aligned(16)));
        int16_t gy_buf[8] __attribute__((aligned(16)));

        for (int i = 0; i < 8; i++)
        {
            int px = x + i;

            // Sobel X勾配（スカラー計算）
            int gx = -1 * y_prev[px - 1] + 1 * y_prev[px + 1] +
                     -2 * y_curr[px - 1] + 2 * y_curr[px + 1] +
                     -1 * y_next[px - 1] + 1 * y_next[px + 1];

            // Sobel Y勾配（スカラー計算）
            int gy = -1 * y_prev[px - 1] - 2 * y_prev[px] - 1 * y_prev[px + 1] +
                     1 * y_next[px - 1] + 2 * y_next[px] + 1 * y_next[px + 1];

            gx_buf[i] = (int16_t)gx;
            gy_buf[i] = (int16_t)gy;
        }

        // ベクトルロード（MVE）
        int16x8_t gx_vec = vld1q_s16(gx_buf);
        int16x8_t gy_vec = vld1q_s16(gy_buf);

        // ベクトル演算で後処理（MVE）
        // 1. 絶対値
        int16x8_t abs_gx = vabsq_s16(gx_vec);
        int16x8_t abs_gy = vabsq_s16(gy_vec);

        // 2. 勾配の大きさ近似: (|gx| + |gy|) / 2
        int16x8_t magnitude = vaddq_s16(abs_gx, abs_gy);
        magnitude = vshrq_n_s16(magnitude, 1); // /2

        // 3. 閾値処理: magnitude < 20 → 0
        mve_pred16_t mask = vcmpgtq_n_s16(magnitude, 19);
        magnitude = vpselq_s16(magnitude, vdupq_n_s16(0), mask);

        // 4. 上限クランプ: magnitude > 255 → 255
        magnitude = vminq_s16(magnitude, vdupq_n_s16(255));

        // 5. 結果をスカラーバッファに保存
        int16_t result_buf[8] __attribute__((aligned(16)));
        vst1q_s16(result_buf, magnitude);

        // 6. 8ビットに変換して出力
        for (int i = 0; i < 8; i++)
        {
            edge_out[x + i] = (uint8_t)result_buf[i];
        }
    }

    // 残りをスカラー処理
    for (; x < FRAME_WIDTH - 1; x++)
    {
        int gx = 0, gy = 0;

        // Sobel X
        gx += -1 * y_prev[x - 1] + 1 * y_prev[x + 1];
        gx += -2 * y_curr[x - 1] + 2 * y_curr[x + 1];
        gx += -1 * y_next[x - 1] + 1 * y_next[x + 1];

        // Sobel Y
        gy += -1 * y_prev[x - 1] - 2 * y_prev[x] - 1 * y_prev[x + 1];
        gy += 1 * y_next[x - 1] + 2 * y_next[x] + 1 * y_next[x + 1];

        int magnitude = (abs(gx) + abs(gy)) / 2;
        if (magnitude < 20)
            magnitude = 0;
        else if (magnitude > 255)
            magnitude = 255;

        edge_out[x] = (uint8_t)magnitude;
    }
}

#else
// 標準版 - Helium MVEなし
static FC128_UNUSED void apply_sobel_filter(uint8_t y_prev[FRAME_WIDTH],
                                            uint8_t y_curr[FRAME_WIDTH],
                                            uint8_t y_next[FRAME_WIDTH],
                                            uint8_t edge_out[FRAME_WIDTH])
{
    const int sobel_x[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    const int sobel_y[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};

    // 境界ピクセルは中央ピクセルの値をそのまま使う
    edge_out[0] = y_curr[0];
    edge_out[FRAME_WIDTH - 1] = y_curr[FRAME_WIDTH - 1];

    for (int x = 1; x < FRAME_WIDTH - 1; x++)
    {
        int gx = 0, gy = 0;

        // 3x3カーネルを適用
        uint8_t *rows[3] = {y_prev, y_curr, y_next};
        for (int ky = 0; ky < 3; ky++)
        {
            for (int kx = 0; kx < 3; kx++)
            {
                int pixel_x = x - 1 + kx;
                int idx = ky * 3 + kx;
                gx += sobel_x[idx] * rows[ky][pixel_x];
                gy += sobel_y[idx] * rows[ky][pixel_x];
            }
        }

        // 勾配の大きさ
        int magnitude = (int)sqrtf((float)(gx * gx + gy * gy));

        // 閾値処理とスケーリング
        magnitude = magnitude / 2; // 強度を1/2に
        if (magnitude < 20)        // 閾値以下は0（ノイズ除去）
            magnitude = 0;
        else if (magnitude > 255)
            magnitude = 255;

        edge_out[x] = (uint8_t)magnitude;
    }
}
#endif // USE_HELIUM_MVE

/* Shape from Shading用のp,q勾配を計算
 * p = ∂z/∂x (x方向の表面勾配)
 * q = ∂z/∂y (y方向の表面勾配)
 *
 * 反射率方程式: R(x,y) = ρ(x,y) * (p*ps + q*qs + 1) / sqrt((1+p²+q²)(1+ps²+qs²+ts²))
 * 光源定数の場合、正規化輝度 E = R/ρ から:
 * E * sqrt(1+p²+q²) * sqrt(1+ps²+qs²+ts²) = p*ps + q*qs + 1
 *
 * 簡易推定: I(x,y) ≈ (1 - p*Gx - q*Gy) として、ローカル勾配から推定
 */
static FC128_UNUSED void compute_pq_gradients(uint8_t y_prev[FRAME_WIDTH], uint8_t y_curr[FRAME_WIDTH],
                                              uint8_t y_next[FRAME_WIDTH], uint8_t pq_out[FRAME_WIDTH * 2])
{
    // 最初と最後のピクセルは0に設定
    pq_out[0] = 0;                   // q[0]
    pq_out[1] = 0;                   // p[0]
    pq_out[FRAME_WIDTH * 2 - 2] = 0; // q[last]
    pq_out[FRAME_WIDTH * 2 - 1] = 0; // p[last]

#if USE_HELIUM_MVE
    // MVE版: 8ピクセル単位でSobelフィルタを並列処理
    int x;
    for (x = 1; x < FRAME_WIDTH - 8; x += 8)
    {
        // 3行×10ピクセル（中央8 + 左右各1）をロード
        int16x8_t gx_vec = vdupq_n_s16(0);
        int16x8_t gy_vec = vdupq_n_s16(0);

        // 上段（y_prev）: Sobel_x: [-1, 0, 1], Sobel_y: [-1, -2, -1]
        uint16x8_t prev_left = vmovlbq_u8(vld1q_u8(&y_prev[x - 1]));  // 左シフト
        uint16x8_t prev_center = vmovlbq_u8(vld1q_u8(&y_prev[x]));    // 中央
        uint16x8_t prev_right = vmovlbq_u8(vld1q_u8(&y_prev[x + 1])); // 右シフト

        gx_vec = vsubq_s16(gx_vec, vreinterpretq_s16_u16(prev_left));                   // -1 * left
        gx_vec = vaddq_s16(gx_vec, vreinterpretq_s16_u16(prev_right));                  // +1 * right
        gy_vec = vsubq_s16(gy_vec, vreinterpretq_s16_u16(prev_left));                   // -1 * left
        gy_vec = vsubq_s16(gy_vec, vshlq_n_s16(vreinterpretq_s16_u16(prev_center), 1)); // -2 * center
        gy_vec = vsubq_s16(gy_vec, vreinterpretq_s16_u16(prev_right));                  // -1 * right

        // 中段（y_curr）: Sobel_x: [-2, 0, 2], Sobel_y: [0, 0, 0]
        uint16x8_t curr_left = vmovlbq_u8(vld1q_u8(&y_curr[x - 1]));
        uint16x8_t curr_right = vmovlbq_u8(vld1q_u8(&y_curr[x + 1]));

        gx_vec = vsubq_s16(gx_vec, vshlq_n_s16(vreinterpretq_s16_u16(curr_left), 1));  // -2 * left
        gx_vec = vaddq_s16(gx_vec, vshlq_n_s16(vreinterpretq_s16_u16(curr_right), 1)); // +2 * right

        // 下段（y_next）: Sobel_x: [-1, 0, 1], Sobel_y: [1, 2, 1]
        uint16x8_t next_left = vmovlbq_u8(vld1q_u8(&y_next[x - 1]));
        uint16x8_t next_center = vmovlbq_u8(vld1q_u8(&y_next[x]));
        uint16x8_t next_right = vmovlbq_u8(vld1q_u8(&y_next[x + 1]));

        gx_vec = vsubq_s16(gx_vec, vreinterpretq_s16_u16(next_left));                   // -1 * left
        gx_vec = vaddq_s16(gx_vec, vreinterpretq_s16_u16(next_right));                  // +1 * right
        gy_vec = vaddq_s16(gy_vec, vreinterpretq_s16_u16(next_left));                   // +1 * left
        gy_vec = vaddq_s16(gy_vec, vshlq_n_s16(vreinterpretq_s16_u16(next_center), 1)); // +2 * center
        gy_vec = vaddq_s16(gy_vec, vreinterpretq_s16_u16(next_right));                  // +1 * right

        // ÷8でスケーリング（算術右シフト）
        int16x8_t p_vec = vshrq_n_s16(gx_vec, 3);
        int16x8_t q_vec = vshrq_n_s16(gy_vec, 3);

        // 範囲制限: [-127, 127]
        p_vec = vmaxq_s16(p_vec, vdupq_n_s16(-127));
        p_vec = vminq_s16(p_vec, vdupq_n_s16(127));
        q_vec = vmaxq_s16(q_vec, vdupq_n_s16(-127));
        q_vec = vminq_s16(q_vec, vdupq_n_s16(127));

        // +127でオフセット
        p_vec = vaddq_s16(p_vec, vdupq_n_s16(127));
        q_vec = vaddq_s16(q_vec, vdupq_n_s16(127));

        // int16 → uint8に変換して保存
        int16_t p_arr[8], q_arr[8];
        vst1q_s16(p_arr, p_vec);
        vst1q_s16(q_arr, q_vec);

        // インターリーブして保存: q0, p0, q1, p1, ...
        for (int i = 0; i < 8; i++)
        {
            pq_out[(x + i) * 2] = (uint8_t)q_arr[i];     // q
            pq_out[(x + i) * 2 + 1] = (uint8_t)p_arr[i]; // p
        }
    }

    // 残りをスカラー処理
    for (; x < FRAME_WIDTH - 1; x++)
    {
        int gx = 0, gy = 0;

        // 3x3カーネルを適用
        uint8_t *rows[3] = {y_prev, y_curr, y_next};
        const int sobel_x[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
        const int sobel_y[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};

        for (int ky = 0; ky < 3; ky++)
        {
            for (int kx = 0; kx < 3; kx++)
            {
                int pixel_x = x - 1 + kx;
                int idx = ky * 3 + kx;
                gx += sobel_x[idx] * rows[ky][pixel_x];
                gy += sobel_y[idx] * rows[ky][pixel_x];
            }
        }

        int p = gx / 8;
        int q = gy / 8;

        if (p < -127)
            p = -127;
        if (p > 127)
            p = 127;
        if (q < -127)
            q = -127;
        if (q > 127)
            q = 127;

        pq_out[x * 2] = (uint8_t)(q + 127);
        pq_out[x * 2 + 1] = (uint8_t)(p + 127);
    }
#else
    // スカラー版
    const int sobel_x[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    const int sobel_y[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};

    for (int x = 1; x < FRAME_WIDTH - 1; x++)
    {
        int gx = 0, gy = 0;

        uint8_t *rows[3] = {y_prev, y_curr, y_next};
        for (int ky = 0; ky < 3; ky++)
        {
            for (int kx = 0; kx < 3; kx++)
            {
                int pixel_x = x - 1 + kx;
                int idx = ky * 3 + kx;
                gx += sobel_x[idx] * rows[ky][pixel_x];
                gy += sobel_y[idx] * rows[ky][pixel_x];
            }
        }

        int p = gx / 8;
        int q = gy / 8;

        if (p < -127)
            p = -127;
        if (p > 127)
            p = 127;
        if (q < -127)
            q = -127;
        if (q > 127)
            q = 127;

        pq_out[x * 2] = (uint8_t)(q + 127);
        pq_out[x * 2 + 1] = (uint8_t)(p + 127);
    }
#endif
}

/* 簡易深度復元（行方向積分）
 * p,q勾配から深度マップを生成
 * 簡易版：行ごとにp勾配を積分（∫p dx）
 */
#if USE_HELIUM_MVE
// Helium MVE版 - 真のベクトル命令による高速化
static FC128_UNUSED void reconstruct_depth_simple(uint8_t pq_data[FRAME_WIDTH * 2], uint8_t depth_line[FRAME_WIDTH])
{
    float z = 0.0f;           // 深度の累積値
    const float scale = 2.0f; // スケーリングファクタ

    // 16ピクセル単位でMVEベクトル処理（128-bit）
    int x;
    for (x = 0; x < FRAME_WIDTH - 15; x += 16)
    {
        // 16ピクセル分のp勾配を抽出（128-bit MVEロード用）
        uint8_t p_bytes[16] __attribute__((aligned(16)));
        for (int i = 0; i < 16; i++)
        {
            p_bytes[i] = pq_data[(x + i) * 2 + 1]; // p成分
        }

        // MVEベクトルロード（128-bit = 16バイト）
        uint8x16_t p_u8_vec = vld1q_u8(p_bytes);

        // uint8 → int16 拡張（MVE: 下位8バイト）
        int16x8_t p_s16_low = vreinterpretq_s16_u16(vmovlbq_u8(p_u8_vec));

        // -127オフセット適用（MVEベクトル減算）
        int16x8_t offset = vdupq_n_s16(127);
        int16x8_t p_adjusted_low = vsubq_s16(p_s16_low, offset);

        // ベクトルをスカラーに展開
        int16_t p_raw[8] __attribute__((aligned(16)));
        vst1q_s16(p_raw, p_adjusted_low);

        // 積分処理（前半8ピクセル）
        for (int i = 0; i < 8; i++)
        {
            z += (float)p_raw[i] * scale;

            // 積分値が極端になりすぎないように制限
            if (z < -200.0f)
                z = -200.0f;
            if (z > 200.0f)
                z = 200.0f;

            int depth_val = (int)(z + 128.0f);
            if (depth_val < 0)
                depth_val = 0;
            if (depth_val > 255)
                depth_val = 255;

            depth_line[x + i] = (uint8_t)depth_val;
        }

        // uint8 → int16 拡張（MVE: 上位8バイト）
        int16x8_t p_s16_high = vreinterpretq_s16_u16(vmovltq_u8(p_u8_vec));
        int16x8_t p_adjusted_high = vsubq_s16(p_s16_high, offset);
        vst1q_s16(p_raw, p_adjusted_high);

        // 積分処理（後半8ピクセル）
        for (int i = 0; i < 8; i++)
        {
            z += (float)p_raw[i] * scale;

            // 積分値が極端になりすぎないように制限
            if (z < -200.0f)
                z = -200.0f;
            if (z > 200.0f)
                z = 200.0f;

            int depth_val = (int)(z + 128.0f);
            if (depth_val < 0)
                depth_val = 0;
            if (depth_val > 255)
                depth_val = 255;

            depth_line[x + 8 + i] = (uint8_t)depth_val;
        }
    }

    // 残りをスカラー処理
    for (; x < FRAME_WIDTH; x++)
    {
        int p_raw = (int)pq_data[x * 2 + 1] - 127;
        z += (float)p_raw * scale;

        // 積分値が極端になりすぎないように制限
        if (z < -200.0f)
            z = -200.0f;
        if (z > 200.0f)
            z = 200.0f;

        int depth_val = (int)(z + 128.0f);
        if (depth_val < 0)
            depth_val = 0;
        if (depth_val > 255)
            depth_val = 255;

        depth_line[x] = (uint8_t)depth_val;
    }
}
#else
// 標準版（MVEなし）
static FC128_UNUSED void reconstruct_depth_simple(uint8_t pq_data[FRAME_WIDTH * 2], uint8_t depth_line[FRAME_WIDTH])
{
    // p勾配を符号付きに戻す（0〜254 → -127〜+127）
    float z = 0.0f;           // 深度の累積値
    const float scale = 2.0f; // スケーリングファクタ

    for (int x = 0; x < FRAME_WIDTH; x++)
    {
        // p勾配を取得（偶数インデックス）
        int p_raw = (int)pq_data[x * 2 + 1] - 127; // -127〜+127

        // 深度を積分（z += p * dx、ここでdx=1ピクセル）
        z += (float)p_raw * scale;

        // 0〜255の範囲にマッピング
        int depth_val = (int)(z + 128.0f);
        if (depth_val < 0)
            depth_val = 0;
        if (depth_val > 255)
            depth_val = 255;

        depth_line[x] = (uint8_t)depth_val;
    }
}
#endif

#if USE_SIMPLE_DIRECT_P
/* HyperRAMから直接p勾配をストリーミングして行積分する簡易版。
 * USE_SIMPLE_DIRECT_P=0で従来のSRAMバッファ経由に戻せる。 */
static FC128_UNUSED void reconstruct_depth_simple_direct(uint32_t gradient_line_offset, uint8_t depth_line[FRAME_WIDTH])
{
    float z = 0.0f;
    const float scale = 2.0f;
    uint8_t chunk[128];
    uint32_t remaining = FRAME_WIDTH * 2;
    uint32_t offset = 0;
    int pixel = 0;

    while (remaining > 0 && pixel < FRAME_WIDTH)
    {
        uint32_t chunk_size = (remaining > sizeof(chunk)) ? (uint32_t)sizeof(chunk) : remaining;
        chunk_size &= ~1U; // 偶数バイト境界を維持

        fsp_err_t err = hyperram_b_read(chunk, (void *)(gradient_line_offset + offset), chunk_size);
        if (FSP_SUCCESS != err)
        {
            xprintf("[Thread3] Direct depth read failed at offset=%d err=%d\n",
                    (unsigned long)(gradient_line_offset + offset), err);
            memset(depth_line + pixel, 0, (size_t)(FRAME_WIDTH - pixel));
            return;
        }

        for (uint32_t i = 0; i < chunk_size && pixel < FRAME_WIDTH; i += 2)
        {
            int p_raw = (int)chunk[i + 1] - 127;
            z += (float)p_raw * scale;

            if (z < -200.0f)
            {
                z = -200.0f;
            }
            else if (z > 200.0f)
            {
                z = 200.0f;
            }

            int depth_val = (int)(z + 128.0f);
            if (depth_val < 0)
            {
                depth_val = 0;
            }
            else if (depth_val > 255)
            {
                depth_val = 255;
            }

            depth_line[pixel++] = (uint8_t)depth_val;
        }

        remaining -= chunk_size;
        offset += chunk_size;
    }
}
#endif

/* エッジ強度をYUV422形式に変換（MATLAB yuv422_to_rgb_fast完全対応）
 * Y = エッジ強度, U = V = 128（グレースケール）
 * YUV422フォーマット（リトルエンディアン）:
 *   8バイト = 4ピクセル
 *   [V0 Y1 U0 Y0] [V1 Y3 U1 Y2]
 * MATLAB yuv422_to_rgb_fast デコード順序:
 *   block(8)=Y2 → pix1, block(6)=Y3 → pix2, block(4)=Y0 → pix3, block(2)=Y1 → pix4
 * つまり: pix1=edge[x], pix2=edge[x+1], pix3=edge[x+2], pix4=edge[x+3]
 *
 * 必要なエンコード:
 *   block(2)=Y1=edge[x+3], block(4)=Y0=edge[x+2], block(6)=Y3=edge[x+1], block(8)=Y2=edge[x]
 */
static FC128_UNUSED void edge_to_yuv422(uint8_t edge_line[FRAME_WIDTH], uint8_t yuv_line[FRAME_WIDTH * 2])
{
    // 4ピクセル（8バイト）単位で処理
    for (int x = 0; x < FRAME_WIDTH; x += 4)
    {
        int yuv_index = x * 2; // 8バイト単位のオフセット

        // MATLABのデコード: pix1=Y2, pix2=Y3, pix3=Y0, pix4=Y1
        // edge_line順で出力するため: Y2=edge[x], Y3=edge[x+1], Y0=edge[x+2], Y1=edge[x+3]

        // 1つ目の4バイト: [V0 Y1 U0 Y0]
        yuv_line[yuv_index + 0] = 128;              // V0 = 128 (pix3,4で使用)
        yuv_line[yuv_index + 1] = edge_line[x + 3]; // Y1 = edge[x+3] → pix4
        yuv_line[yuv_index + 2] = 128;              // U0 = 128 (pix3,4で使用)
        yuv_line[yuv_index + 3] = edge_line[x + 2]; // Y0 = edge[x+2] → pix3

        // 2つ目の4バイト: [V1 Y3 U1 Y2]
        yuv_line[yuv_index + 4] = 128;              // V1 = 128 (pix1,2で使用)
        yuv_line[yuv_index + 5] = edge_line[x + 1]; // Y3 = edge[x+1] → pix2
        yuv_line[yuv_index + 6] = 128;              // U1 = 128 (pix1,2で使用)
        yuv_line[yuv_index + 7] = edge_line[x];     // Y2 = edge[x] → pix1
    }
}

// ========== マルチグリッド法による深度復元 ==========
#if USE_DEPTH_METHOD == 1

static const int pre_smooth = 3;
static const int post_smooth = 3;
static const int coarse_iter = 200; // 最粗レベルで十分に収束させる
static const int mg_cycles = 5;     // V-cycleを複数回実行

static void mg_prepare_layout(void)
{
    if (g_mg_layout_ready)
    {
        return;
    }

    uint32_t offset = MG_WORK_OFFSET;
    int width = FRAME_WIDTH;
    int height = FRAME_HEIGHT;
    g_mg_level_count = 0;

    while (g_mg_level_count < MG_MAX_LEVELS)
    {
        mg_level_t *level = &g_mg_levels[g_mg_level_count];
        level->width = width;
        level->height = height;
        level->z_offset = offset;
        offset += (uint32_t)((uint32_t)width * (uint32_t)height * (uint32_t)sizeof(float));
        level->rhs_offset = offset;
        offset += (uint32_t)((uint32_t)width * (uint32_t)height * (uint32_t)sizeof(float));
        level->residual_offset = offset; // 各レベル専用の残差バッファ
        offset += (uint32_t)((uint32_t)width * (uint32_t)height * (uint32_t)sizeof(float));

        g_mg_level_count++;

        if (width <= 80 || height <= 60)
        {
            break;
        }

        width = (width + 1) / 2;
        height = (height + 1) / 2;
    }

    uint32_t total_size = offset;

    xprintf("[MG Layout] Total levels: %d\n", g_mg_level_count);
    xprintf("[MG Layout] Work offset: 0x%08X\n", MG_WORK_OFFSET);
    xprintf("[MG Layout] Total size: %u bytes (%.2f MB)\n", total_size, (float)total_size / (1024.0f * 1024.0f));
    xprintf("[MG Layout] HyperRAM capacity: 8 MB\n");

    if (total_size > 8 * 1024 * 1024)
    {
        xprintf("[MG Layout] WARNING: Exceeds HyperRAM capacity!\n");
    }

    g_mg_layout_ready = true;
}

static void mg_zero_buffer(uint32_t offset, size_t length)
{
    uint8_t zero_block[128] = {0};
    size_t written = 0;

    while (written < length)
    {
        size_t chunk = length - written;
        if (chunk > sizeof(zero_block))
        {
            chunk = sizeof(zero_block);
        }

        hyperram_b_write(zero_block, (void *)(offset + written), (uint32_t)chunk);
        written += chunk;
    }
}

static void mg_zero_all_levels(void)
{
    for (int i = 0; i < g_mg_level_count; i++)
    {
        size_t bytes = (size_t)g_mg_levels[i].width * (size_t)g_mg_levels[i].height * sizeof(float);
        mg_zero_buffer(g_mg_levels[i].z_offset, bytes);
        mg_zero_buffer(g_mg_levels[i].rhs_offset, bytes);
        mg_zero_buffer(g_mg_levels[i].residual_offset, bytes);
    }
}

static void mg_compute_divergence_to_hyperram(const mg_level_t *level)
{
    const int width = level->width;
    const int height = level->height;
    const uint32_t pq_row_bytes = FRAME_WIDTH * 2;
    const uint32_t rhs_row_bytes = (uint32_t)width * sizeof(float);

    uint8_t pq_prev[FRAME_WIDTH * 2];
    uint8_t pq_curr[FRAME_WIDTH * 2];
    uint8_t pq_next[FRAME_WIDTH * 2];
    float div_row[FRAME_WIDTH];

    hyperram_b_read(pq_curr, (void *)GRADIENT_OFFSET, pq_row_bytes);
    memcpy(pq_prev, pq_curr, pq_row_bytes);
    if (height > 1)
    {
        hyperram_b_read(pq_next, (void *)(GRADIENT_OFFSET + pq_row_bytes), pq_row_bytes);
    }
    else
    {
        memcpy(pq_next, pq_curr, pq_row_bytes);
    }

    for (int y = 0; y < height; y++)
    {
        if (y == 0 || y == height - 1)
        {
            memset(div_row, 0, rhs_row_bytes);
        }
        else
        {
            div_row[0] = 0.0f;
            div_row[width - 1] = 0.0f;

#if USE_HELIUM_MVE
            // MVE版: 8ピクセル単位で処理（発散計算の高速化）
            // div = -(∂p/∂x + ∂q/∂y) = -((p_curr - p_prev) + (q_curr - q_prev))
            int x;
            for (x = 1; x < width - 8; x += 8)
            {
                // p成分を抽出（8ピクセル分）
                uint8_t p_curr_buf[8] __attribute__((aligned(16)));
                uint8_t p_prev_buf[8] __attribute__((aligned(16)));
                uint8_t q_curr_buf[8] __attribute__((aligned(16)));
                uint8_t q_prev_buf[8] __attribute__((aligned(16)));

                for (int i = 0; i < 8; i++)
                {
                    p_curr_buf[i] = pq_curr[(x + i) * 2 + 1];
                    p_prev_buf[i] = pq_curr[(x + i - 1) * 2 + 1];
                    q_curr_buf[i] = pq_curr[(x + i) * 2];
                    q_prev_buf[i] = pq_prev[(x + i) * 2];
                }

                // MVEベクトルロード
                uint8x16_t p_curr_u8 = vld1q_u8(p_curr_buf);
                uint8x16_t p_prev_u8 = vld1q_u8(p_prev_buf);
                uint8x16_t q_curr_u8 = vld1q_u8(q_curr_buf);
                uint8x16_t q_prev_u8 = vld1q_u8(q_prev_buf);

                // uint8 → int16 拡張（下位8バイト）
                int16x8_t p_curr_s16 = vreinterpretq_s16_u16(vmovlbq_u8(p_curr_u8));
                int16x8_t p_prev_s16 = vreinterpretq_s16_u16(vmovlbq_u8(p_prev_u8));
                int16x8_t q_curr_s16 = vreinterpretq_s16_u16(vmovlbq_u8(q_curr_u8));
                int16x8_t q_prev_s16 = vreinterpretq_s16_u16(vmovlbq_u8(q_prev_u8));

                // -127オフセット適用
                int16x8_t offset = vdupq_n_s16(127);
                p_curr_s16 = vsubq_s16(p_curr_s16, offset);
                p_prev_s16 = vsubq_s16(p_prev_s16, offset);
                q_curr_s16 = vsubq_s16(q_curr_s16, offset);
                q_prev_s16 = vsubq_s16(q_prev_s16, offset);

                // ∂p/∂x = p_curr - p_prev
                int16x8_t dp_dx = vsubq_s16(p_curr_s16, p_prev_s16);

                // ∂q/∂y = q_curr - q_prev
                int16x8_t dq_dy = vsubq_s16(q_curr_s16, q_prev_s16);

                // div = -(dp/dx + dq/dy)
                int16x8_t div_s16 = vaddq_s16(dp_dx, dq_dy);
                div_s16 = vnegq_s16(div_s16);

                // int16 → float変換
                int16_t div_i16[8] __attribute__((aligned(16)));
                vst1q_s16(div_i16, div_s16);

                for (int i = 0; i < 8; i++)
                {
                    div_row[x + i] = (float)div_i16[i];
                }
            }

            // 残りをスカラー処理
            for (; x < width - 1; x++)
            {
                int p_curr = (int)pq_curr[x * 2 + 1] - 127;
                int p_prev = (int)pq_curr[(x - 1) * 2 + 1] - 127;
                int q_curr = (int)pq_curr[x * 2] - 127;
                int q_prev = (int)pq_prev[x * 2] - 127;
                div_row[x] = -(float)(p_curr - p_prev + q_curr - q_prev);
            }
#else
            // スカラー版
            for (int x = 1; x < width - 1; x++)
            {
                int p_curr = (int)pq_curr[x * 2 + 1] - 127;
                int p_prev = (int)pq_curr[(x - 1) * 2 + 1] - 127;
                int q_curr = (int)pq_curr[x * 2] - 127;
                int q_prev = (int)pq_prev[x * 2] - 127;
                div_row[x] = -(float)(p_curr - p_prev + q_curr - q_prev);
            }
#endif
        }

        hyperram_b_write(div_row, (void *)(level->rhs_offset + (uint32_t)y * rhs_row_bytes), rhs_row_bytes);

        if (y == height - 1)
        {
            break;
        }

        memcpy(pq_prev, pq_curr, pq_row_bytes);
        memcpy(pq_curr, pq_next, pq_row_bytes);

        int next_row = y + 2;
        if (next_row >= height)
        {
            memcpy(pq_next, pq_curr, pq_row_bytes);
        }
        else
        {
            hyperram_b_read(pq_next, (void *)(GRADIENT_OFFSET + (uint32_t)next_row * pq_row_bytes), pq_row_bytes);
        }
    }
}

static void mg_gauss_seidel(const mg_level_t *level, int iterations)
{
    const int width = level->width;
    const int height = level->height;

    if (width < 2 || height < 2)
    {
        return;
    }

    const uint32_t row_bytes = (uint32_t)width * sizeof(float);
    float row_prev[FRAME_WIDTH];
    float row_curr[FRAME_WIDTH];
    float row_next[FRAME_WIDTH];
    float rhs_curr[FRAME_WIDTH];

    for (int iter = 0; iter < iterations; iter++)
    {
        for (int color = 0; color < 2; color++)
        {
            hyperram_b_read(row_prev, (void *)(level->z_offset + 0), row_bytes);
            hyperram_b_read(row_curr, (void *)(level->z_offset + row_bytes), row_bytes);

            if (height > 2)
            {
                hyperram_b_read(row_next, (void *)(level->z_offset + 2 * row_bytes), row_bytes);
            }
            else
            {
                memcpy(row_next, row_curr, row_bytes);
            }

            for (int y = 1; y < height - 1; y++)
            {
                hyperram_b_read(rhs_curr, (void *)(level->rhs_offset + y * row_bytes), row_bytes);
                int start_x = 1 + ((y + color) & 1);

#if USE_MVE_FOR_GAUSS_SEIDEL
                // MVE版: 4ピクセル並列処理（Red-Blackパターン、stride=2）
                int x;
                for (x = start_x; x < width - 8; x += 8)
                {
                    // 4つのRed/Blackピクセル（x, x+2, x+4, x+6）とその隣接ピクセルをロード
                    // 左隣: x-1, x+1, x+3, x+5
                    float32x4_t left = {row_curr[x - 1], row_curr[x + 1], row_curr[x + 3], row_curr[x + 5]};
                    // 右隣: x+1, x+3, x+5, x+7
                    float32x4_t right = {row_curr[x + 1], row_curr[x + 3], row_curr[x + 5], row_curr[x + 7]};
                    // 上隣: x, x+2, x+4, x+6
                    float32x4_t above = {row_prev[x], row_prev[x + 2], row_prev[x + 4], row_prev[x + 6]};
                    // 下隣: x, x+2, x+4, x+6
                    float32x4_t below = {row_next[x], row_next[x + 2], row_next[x + 4], row_next[x + 6]};
                    // RHS: x, x+2, x+4, x+6
                    float32x4_t rhs_vec = {rhs_curr[x], rhs_curr[x + 2], rhs_curr[x + 4], rhs_curr[x + 6]};

                    // neighbor_sum = left + right + above + below
                    float32x4_t sum = vaddq_f32(left, right);
                    sum = vaddq_f32(sum, above);
                    sum = vaddq_f32(sum, below);

                    // result = 0.25 * (neighbor_sum - rhs)
                    sum = vsubq_f32(sum, rhs_vec);
                    float32x4_t result = vmulq_n_f32(sum, 0.25f);

                    // ストア（stride=2でスカラー保存）
                    float result_arr[4];
                    vst1q_f32(result_arr, result);
                    row_curr[x] = result_arr[0];
                    row_curr[x + 2] = result_arr[1];
                    row_curr[x + 4] = result_arr[2];
                    row_curr[x + 6] = result_arr[3];
                }

                // 残りをスカラー処理
                for (; x < width - 1; x += 2)
                {
                    float neighbor_sum = row_curr[x - 1] + row_curr[x + 1] + row_prev[x] + row_next[x];
                    row_curr[x] = 0.25f * (neighbor_sum - rhs_curr[x]);
                }
#else
                // スカラー版
                for (int x = start_x; x < width - 1; x += 2)
                {
                    float neighbor_sum = row_curr[x - 1] + row_curr[x + 1] + row_prev[x] + row_next[x];
                    row_curr[x] = 0.25f * (neighbor_sum - rhs_curr[x]);
                }
#endif

                hyperram_b_write(row_curr, (void *)(level->z_offset + y * row_bytes), row_bytes);

                if (y < height - 2)
                {
                    memcpy(row_prev, row_curr, row_bytes);
                    memcpy(row_curr, row_next, row_bytes);
                    int next_index = y + 2;
                    if (next_index < height)
                    {
                        hyperram_b_read(row_next, (void *)(level->z_offset + next_index * row_bytes), row_bytes);
                    }
                    else
                    {
                        memcpy(row_next, row_curr, row_bytes);
                    }
                }
            }
        }

        // Red-Black両パス完了後、Neumann境界条件を適用
        // 上下境界: y=0とy=height-1を隣接行で設定
        hyperram_b_read(row_curr, (void *)(level->z_offset + row_bytes), row_bytes); // y=1
        hyperram_b_write(row_curr, (void *)(level->z_offset + 0), row_bytes);        // y=0 = y=1

        hyperram_b_read(row_curr, (void *)(level->z_offset + (height - 2) * row_bytes), row_bytes);  // y=h-2
        hyperram_b_write(row_curr, (void *)(level->z_offset + (height - 1) * row_bytes), row_bytes); // y=h-1 = y=h-2

        // 左右境界: 全行でx=0とx=width-1を隣接ピクセルで設定
        for (int y = 0; y < height; y++)
        {
            hyperram_b_read(row_curr, (void *)(level->z_offset + y * row_bytes), row_bytes);
            row_curr[0] = row_curr[1];
            row_curr[width - 1] = row_curr[width - 2];
            hyperram_b_write(row_curr, (void *)(level->z_offset + y * row_bytes), row_bytes);
        }
    }
}

static void mg_compute_residual(const mg_level_t *level, uint32_t residual_offset)
{
    const int width = level->width;
    const int height = level->height;
    const uint32_t row_bytes = (uint32_t)width * sizeof(float);

    float row_prev[FRAME_WIDTH];
    float row_curr[FRAME_WIDTH];
    float row_next[FRAME_WIDTH];
    float rhs_curr[FRAME_WIDTH];
    float res_row[FRAME_WIDTH];

    if (height == 0)
    {
        return;
    }

    hyperram_b_read(row_prev, (void *)(level->z_offset + 0), row_bytes);
    if (height > 1)
    {
        hyperram_b_read(row_curr, (void *)(level->z_offset + row_bytes), row_bytes);
    }
    else
    {
        memcpy(row_curr, row_prev, row_bytes);
    }

    if (height > 2)
    {
        hyperram_b_read(row_next, (void *)(level->z_offset + 2 * row_bytes), row_bytes);
    }
    else
    {
        memcpy(row_next, row_curr, row_bytes);
    }

    memset(res_row, 0, row_bytes);
    hyperram_b_write(res_row, (void *)residual_offset, row_bytes);

    for (int y = 1; y < height - 1; y++)
    {
        hyperram_b_read(rhs_curr, (void *)(level->rhs_offset + (uint32_t)y * row_bytes), row_bytes);
        res_row[0] = 0.0f;
        res_row[width - 1] = 0.0f;

#if USE_HELIUM_MVE
        // MVE版: 4要素単位でLaplacian演算と残差計算
        float32x4_t coeff_4 = vdupq_n_f32(-4.0f);

        int x;
        for (x = 1; x < width - 4; x += 4)
        {
            // 5点ステンシルLaplacian: lap = left + right + top + bottom - 4*center

            // 左隣（x-1, x, x+1, x+2）
            float32x4_t left = vld1q_f32(&row_curr[x - 1]);

            // 右隣（x+1, x+2, x+3, x+4）
            float32x4_t right = vld1q_f32(&row_curr[x + 1]);

            // 中央（x, x+1, x+2, x+3）
            float32x4_t center = vld1q_f32(&row_curr[x]);

            // 上下（x, x+1, x+2, x+3）
            float32x4_t top = vld1q_f32(&row_prev[x]);
            float32x4_t bottom = vld1q_f32(&row_next[x]);

            // Laplacian = left + right + top + bottom - 4*center
            float32x4_t lap = vaddq_f32(left, right);
            lap = vaddq_f32(lap, top);
            lap = vaddq_f32(lap, bottom);
            float32x4_t center_4x = vmulq_f32(center, coeff_4);
            lap = vaddq_f32(lap, center_4x); // lap += (-4 * center)

            // 残差 = rhs - lap
            float32x4_t rhs_vec = vld1q_f32(&rhs_curr[x]);
            float32x4_t res_vec = vsubq_f32(rhs_vec, lap);

            // 結果を保存
            vst1q_f32(&res_row[x], res_vec);
        }

        // 残りをスカラー処理
        for (; x < width - 1; x++)
        {
            float lap = row_curr[x - 1] + row_curr[x + 1] + row_prev[x] + row_next[x] - 4.0f * row_curr[x];
            res_row[x] = rhs_curr[x] - lap;
        }
#else
        // スカラー版
        for (int x = 1; x < width - 1; x++)
        {
            float lap = row_curr[x - 1] + row_curr[x + 1] + row_prev[x] + row_next[x] - 4.0f * row_curr[x];
            res_row[x] = rhs_curr[x] - lap;
        }
#endif

        hyperram_b_write(res_row, (void *)(residual_offset + (uint32_t)y * row_bytes), row_bytes);

        if (y < height - 2)
        {
            memcpy(row_prev, row_curr, row_bytes);
            memcpy(row_curr, row_next, row_bytes);
            hyperram_b_read(row_next, (void *)(level->z_offset + (uint32_t)(y + 2) * row_bytes), row_bytes);
        }
    }

    if (height > 1)
    {
        memset(res_row, 0, row_bytes);
        hyperram_b_write(res_row, (void *)(residual_offset + (uint32_t)(height - 1) * row_bytes), row_bytes);
    }
}

static void mg_restrict_residual(const mg_level_t *fine, const mg_level_t *coarse, uint32_t residual_offset)
{
    const int fine_w = fine->width;
    const int fine_h = fine->height;
    const int coarse_w = coarse->width;
    const int coarse_h = coarse->height;
    const uint32_t fine_row_bytes = (uint32_t)fine_w * sizeof(float);
    const uint32_t coarse_row_bytes = (uint32_t)coarse_w * sizeof(float);

    float row_above[FRAME_WIDTH];
    float row_center[FRAME_WIDTH];
    float row_below[FRAME_WIDTH];
    float coarse_row[FRAME_WIDTH];

    if (fine_h == 0)
    {
        return;
    }

    for (int cy = 0; cy < coarse_h; cy++)
    {
        int fy = cy * 2;

        if (fy == 0)
        {
            hyperram_b_read(row_center, (void *)(residual_offset + fy * fine_row_bytes), fine_row_bytes);
            memcpy(row_above, row_center, fine_row_bytes);
        }
        else
        {
            hyperram_b_read(row_above, (void *)(residual_offset + (fy - 1) * fine_row_bytes), fine_row_bytes);
            hyperram_b_read(row_center, (void *)(residual_offset + fy * fine_row_bytes), fine_row_bytes);
        }

        if (fy + 1 < fine_h)
        {
            hyperram_b_read(row_below, (void *)(residual_offset + (fy + 1) * fine_row_bytes), fine_row_bytes);
        }
        else
        {
            memcpy(row_below, row_center, fine_row_bytes);
        }

#if USE_MVE_FOR_MG_RESTRICT
        // MVE版: 4ピクセル並列処理（9点ステンシル）
        int cx;
        for (cx = 0; cx < coarse_w - 3; cx += 4)
        {
            // 4つの粗グリッド点に対応する細グリッド点: fx=cx*2
            int fx0 = cx * 2, fx1 = (cx + 1) * 2, fx2 = (cx + 2) * 2, fx3 = (cx + 3) * 2;

            // 中央点×4
            float32x4_t sum = {row_center[fx0] * 4.0f, row_center[fx1] * 4.0f,
                               row_center[fx2] * 4.0f, row_center[fx3] * 4.0f};

            // 左隣×2
            if (fx0 > 0)
            {
                float32x4_t left = {row_center[fx0 - 1] * 2.0f, row_center[fx1 - 1] * 2.0f,
                                    row_center[fx2 - 1] * 2.0f, row_center[fx3 - 1] * 2.0f};
                sum = vaddq_f32(sum, left);
            }
            // 右隣×2
            if (fx3 < fine_w - 1)
            {
                float32x4_t right = {row_center[fx0 + 1] * 2.0f, row_center[fx1 + 1] * 2.0f,
                                     row_center[fx2 + 1] * 2.0f, row_center[fx3 + 1] * 2.0f};
                sum = vaddq_f32(sum, right);
            }
            // 上隣×2
            if (fy > 0)
            {
                float32x4_t above = {row_above[fx0] * 2.0f, row_above[fx1] * 2.0f,
                                     row_above[fx2] * 2.0f, row_above[fx3] * 2.0f};
                sum = vaddq_f32(sum, above);
            }
            // 下隣×2
            if (fy < fine_h - 1)
            {
                float32x4_t below = {row_below[fx0] * 2.0f, row_below[fx1] * 2.0f,
                                     row_below[fx2] * 2.0f, row_below[fx3] * 2.0f};
                sum = vaddq_f32(sum, below);
            }
            // 対角4点（条件付き加算は簡略化のためスカラーで処理）
            float result[4];
            vst1q_f32(result, sum);
            for (int i = 0; i < 4; i++)
            {
                int fx = (cx + i) * 2;
                float s = result[i];
                if (fx > 0 && fy > 0)
                    s += row_above[fx - 1];
                if (fx < fine_w - 1 && fy > 0)
                    s += row_above[fx + 1];
                if (fx > 0 && fy < fine_h - 1)
                    s += row_below[fx - 1];
                if (fx < fine_w - 1 && fy < fine_h - 1)
                    s += row_below[fx + 1];
                coarse_row[cx + i] = s / 16.0f;
            }
        }

        // 残りをスカラー処理
        for (; cx < coarse_w; cx++)
        {
            int fx = cx * 2;
            float sum = row_center[fx] * 4.0f;
            if (fx > 0)
                sum += row_center[fx - 1] * 2.0f;
            if (fx < fine_w - 1)
                sum += row_center[fx + 1] * 2.0f;
            if (fy > 0)
                sum += row_above[fx] * 2.0f;
            if (fy < fine_h - 1)
                sum += row_below[fx] * 2.0f;
            if (fx > 0 && fy > 0)
                sum += row_above[fx - 1];
            if (fx < fine_w - 1 && fy > 0)
                sum += row_above[fx + 1];
            if (fx > 0 && fy < fine_h - 1)
                sum += row_below[fx - 1];
            if (fx < fine_w - 1 && fy < fine_h - 1)
                sum += row_below[fx + 1];
            coarse_row[cx] = sum / 16.0f;
        }
#else
        // スカラー版
        for (int cx = 0; cx < coarse_w; cx++)
        {
            int fx = cx * 2;
            float sum = row_center[fx] * 4.0f;

            if (fx > 0)
            {
                sum += row_center[fx - 1] * 2.0f;
            }
            if (fx < fine_w - 1)
            {
                sum += row_center[fx + 1] * 2.0f;
            }
            if (fy > 0)
            {
                sum += row_above[fx] * 2.0f;
            }
            if (fy < fine_h - 1)
            {
                sum += row_below[fx] * 2.0f;
            }
            if (fx > 0 && fy > 0)
            {
                sum += row_above[fx - 1];
            }
            if (fx < fine_w - 1 && fy > 0)
            {
                sum += row_above[fx + 1];
            }
            if (fx > 0 && fy < fine_h - 1)
            {
                sum += row_below[fx - 1];
            }
            if (fx < fine_w - 1 && fy < fine_h - 1)
            {
                sum += row_below[fx + 1];
            }

            coarse_row[cx] = sum / 16.0f;
        }
#endif

        hyperram_b_write(coarse_row, (void *)(coarse->rhs_offset + cy * coarse_row_bytes), coarse_row_bytes);
    }
}

static void mg_prolong_correction(const mg_level_t *coarse, const mg_level_t *fine)
{
    const int coarse_w = coarse->width;
    const int coarse_h = coarse->height;
    const int fine_w = fine->width;
    const int fine_h = fine->height;
    const uint32_t coarse_row_bytes = (uint32_t)coarse_w * sizeof(float);
    const uint32_t fine_row_bytes = (uint32_t)fine_w * sizeof(float);

    float coarse_row[FRAME_WIDTH];
    float fine_row_even[FRAME_WIDTH];
    float fine_row_odd[FRAME_WIDTH];

    for (int cy = 0; cy < coarse_h; cy++)
    {
        int fy = cy * 2;
        hyperram_b_read(coarse_row, (void *)(coarse->z_offset + cy * coarse_row_bytes), coarse_row_bytes);
        hyperram_b_read(fine_row_even, (void *)(fine->z_offset + fy * fine_row_bytes), fine_row_bytes);
        bool has_odd = (fy + 1) < fine_h;
        if (has_odd)
        {
            hyperram_b_read(fine_row_odd, (void *)(fine->z_offset + (fy + 1) * fine_row_bytes), fine_row_bytes);
        }

#if USE_MVE_FOR_MG_RESTRICT
        // MVE版: 4ピクセル並列処理（双線形補間）
        int cx;
        for (cx = 0; cx < coarse_w - 3; cx += 4)
        {
            // 4つの粗グリッド値をロード
            float32x4_t val_vec = vld1q_f32(&coarse_row[cx]);
            float32x4_t val_half = vmulq_n_f32(val_vec, 0.5f);
            float32x4_t val_quarter = vmulq_n_f32(val_vec, 0.25f);

            // 細グリッド位置
            int fx0 = cx * 2, fx1 = (cx + 1) * 2, fx2 = (cx + 2) * 2, fx3 = (cx + 3) * 2;

            // 偶数行: fx位置に加算
            float vals[4];
            vst1q_f32(vals, val_vec);
            fine_row_even[fx0] += vals[0];
            fine_row_even[fx1] += vals[1];
            fine_row_even[fx2] += vals[2];
            fine_row_even[fx3] += vals[3];

            // 偶数行: fx+1位置に0.5倍で加算
            if (fx3 + 1 < fine_w)
            {
                vst1q_f32(vals, val_half);
                fine_row_even[fx0 + 1] += vals[0];
                fine_row_even[fx1 + 1] += vals[1];
                fine_row_even[fx2 + 1] += vals[2];
                fine_row_even[fx3 + 1] += vals[3];
            }

            // 奇数行
            if (has_odd)
            {
                vst1q_f32(vals, val_half);
                fine_row_odd[fx0] += vals[0];
                fine_row_odd[fx1] += vals[1];
                fine_row_odd[fx2] += vals[2];
                fine_row_odd[fx3] += vals[3];

                if (fx3 + 1 < fine_w)
                {
                    vst1q_f32(vals, val_quarter);
                    fine_row_odd[fx0 + 1] += vals[0];
                    fine_row_odd[fx1 + 1] += vals[1];
                    fine_row_odd[fx2 + 1] += vals[2];
                    fine_row_odd[fx3 + 1] += vals[3];
                }
            }
        }

        // 残りをスカラー処理
        for (; cx < coarse_w; cx++)
        {
            int fx = cx * 2;
            float val = coarse_row[cx];
            fine_row_even[fx] += val;
            if (fx + 1 < fine_w)
            {
                fine_row_even[fx + 1] += val * 0.5f;
            }
            if (has_odd)
            {
                fine_row_odd[fx] += val * 0.5f;
                if (fx + 1 < fine_w)
                {
                    fine_row_odd[fx + 1] += val * 0.25f;
                }
            }
        }
#else
        // スカラー版
        for (int cx = 0; cx < coarse_w; cx++)
        {
            int fx = cx * 2;
            float val = coarse_row[cx];
            fine_row_even[fx] += val;
            if (fx + 1 < fine_w)
            {
                fine_row_even[fx + 1] += val * 0.5f;
            }
            if (has_odd)
            {
                fine_row_odd[fx] += val * 0.5f;
                if (fx + 1 < fine_w)
                {
                    fine_row_odd[fx + 1] += val * 0.25f;
                }
            }
        }
#endif

        hyperram_b_write(fine_row_even, (void *)(fine->z_offset + fy * fine_row_bytes), fine_row_bytes);
        if (has_odd)
        {
            hyperram_b_write(fine_row_odd, (void *)(fine->z_offset + (fy + 1) * fine_row_bytes), fine_row_bytes);
        }
    }
}

static void mg_vcycle(int level_index)
{
    const mg_level_t *level = &g_mg_levels[level_index];
    bool is_base_level = (level_index == g_mg_level_count - 1) || (level->width <= 80) || (level->height <= 60);

    if (is_base_level)
    {
        if (level->width == 80)
        {
            xprintf("[MG] Coarsest level: %dx%d, %d iterations\n", level->width, level->height, coarse_iter);
        }
        mg_gauss_seidel(level, coarse_iter);

        // デバッグ: 最粗レベルの解をチェック
        if (level->width == 80)
        {
            float test_row[FRAME_WIDTH];
            hyperram_b_read(test_row,
                            (void *)(level->z_offset + (uint32_t)level->width * (uint32_t)sizeof(float)),
                            (uint32_t)level->width * (uint32_t)sizeof(float));
            xprintf("[MG] After coarse solve: z[1,40]=%.6f\n", test_row[40]);
        }
        return;
    }

    mg_gauss_seidel(level, pre_smooth);

    mg_compute_residual(level, level->residual_offset);
    const mg_level_t *coarse = &g_mg_levels[level_index + 1];
    mg_restrict_residual(level, coarse, level->residual_offset);

    // 粗いレベルのゼロ化を削除 - residualが既にRHSとして設定されている
    // size_t coarse_bytes = (size_t)coarse->width * coarse->height * sizeof(float);
    // mg_zero_buffer(coarse->z_offset, coarse_bytes);

    mg_vcycle(level_index + 1);

    mg_prolong_correction(coarse, level);
    mg_gauss_seidel(level, post_smooth);
}

static void mg_export_depth_map(const mg_level_t *level, float *z_min_out, float *z_max_out)
{
    const int width = level->width;
    const int height = level->height;
    const uint32_t row_bytes = (uint32_t)width * sizeof(float);

    float row_buffer[FRAME_WIDTH];
    uint8_t depth_row[FRAME_WIDTH];
    float z_min = 1e9f;
    float z_max = -1e9f;

    // フェーズ1: Min/Max検索
    for (int y = 0; y < height; y++)
    {
        hyperram_b_read(row_buffer, (void *)(level->z_offset + (uint32_t)y * row_bytes), row_bytes);

#if USE_HELIUM_MVE
        // MVE版: 4要素単位でmin/max検索
        int x;
        for (x = 0; x < width - 3; x += 4)
        {
            float32x4_t vec = vld1q_f32(&row_buffer[x]);

            // ベクトル内の各要素をチェック
            float temp[4];
            vst1q_f32(temp, vec);
            for (int i = 0; i < 4; i++)
            {
                if (temp[i] < z_min)
                    z_min = temp[i];
                if (temp[i] > z_max)
                    z_max = temp[i];
            }
        }

        // 残りをスカラー処理
        for (; x < width; x++)
        {
            if (row_buffer[x] < z_min)
                z_min = row_buffer[x];
            if (row_buffer[x] > z_max)
                z_max = row_buffer[x];
        }
#else
        // スカラー版
        for (int x = 0; x < width; x++)
        {
            if (row_buffer[x] < z_min)
            {
                z_min = row_buffer[x];
            }
            if (row_buffer[x] > z_max)
            {
                z_max = row_buffer[x];
            }
        }
#endif
    }

    float range = z_max - z_min;
    if (range < 1e-6f)
    {
        range = 1.0f;
    }

    // フェーズ2: 正規化とuint8変換
    for (int y = 0; y < height; y++)
    {
        hyperram_b_read(row_buffer, (void *)(level->z_offset + (uint32_t)y * row_bytes), row_bytes);

#if USE_HELIUM_MVE
        // MVE版: 4要素単位で正規化とクランプ
        float32x4_t z_min_vec = vdupq_n_f32(z_min);
        float32x4_t scale_vec = vdupq_n_f32(255.0f / range);
        int32x4_t zero_vec = vdupq_n_s32(0);
        int32x4_t max_vec = vdupq_n_s32(255);

        int x;
        for (x = 0; x < width - 3; x += 4)
        {
            // 正規化: (z - z_min) * (255 / range)
            float32x4_t z_vec = vld1q_f32(&row_buffer[x]);
            float32x4_t normalized = vsubq_f32(z_vec, z_min_vec); // z - z_min
            normalized = vmulq_f32(normalized, scale_vec);        // * (255/range)

            // float → int32変換
            int32x4_t val_i32 = vcvtq_s32_f32(normalized);

            // クランプ: 0 <= val <= 255
            val_i32 = vmaxq_s32(val_i32, zero_vec); // max(val, 0)
            val_i32 = vminq_s32(val_i32, max_vec);  // min(val, 255)

            // int32 → uint8変換
            int32_t temp[4];
            vst1q_s32(temp, val_i32);
            depth_row[x] = (uint8_t)temp[0];
            depth_row[x + 1] = (uint8_t)temp[1];
            depth_row[x + 2] = (uint8_t)temp[2];
            depth_row[x + 3] = (uint8_t)temp[3];
        }

        // 残りをスカラー処理
        for (; x < width; x++)
        {
            float normalized = (row_buffer[x] - z_min) / range;
            int val = (int)(normalized * 255.0f);
            if (val < 0)
                val = 0;
            if (val > 255)
                val = 255;
            depth_row[x] = (uint8_t)val;
        }
#else
        // スカラー版
        for (int x = 0; x < width; x++)
        {
            float normalized = (row_buffer[x] - z_min) / range;
            int val = (int)(normalized * 255.0f);
            if (val < 0)
            {
                val = 0;
            }
            if (val > 255)
            {
                val = 255;
            }
            depth_row[x] = (uint8_t)val;
        }
#endif
        hyperram_b_write(depth_row, (void *)(DEPTH_OFFSET + (uint32_t)y * FRAME_WIDTH), FRAME_WIDTH);
    }

    if (z_min_out)
    {
        *z_min_out = z_min;
    }
    if (z_max_out)
    {
        *z_max_out = z_max;
    }
}

static FC128_UNUSED void reconstruct_depth_multigrid(void)
{
    xprintf("[Thread3] Multigrid: Starting depth reconstruction\n");
    mg_prepare_layout();

    if (g_mg_level_count == 0)
    {
        xprintf("[Thread3] Multigrid: layout generation failed\n");
        return;
    }

    mg_zero_all_levels();

    const mg_level_t *fine_level = &g_mg_levels[0];
    mg_compute_divergence_to_hyperram(fine_level);

    for (int cycle = 0; cycle < mg_cycles; cycle++)
    {
        mg_vcycle(0);
    }

    float z_min = 0.0f;
    float z_max = 0.0f;
    mg_export_depth_map(fine_level, &z_min, &z_max);
    xprintf("[Thread3] Multigrid: Complete (range: %.2f - %.2f)\n", z_min, z_max);
}

#endif // USE_DEPTH_METHOD == 1

/* Main Thread3 entry function */
/* pvParameters contains TaskHandle_t */
void main_thread3_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

#if APP_MODE_FFT_VERIFY
    /* Ensure printf output works even though thread0 is idle. */
    xdev_out(putchar_ra8usb);
    xprintf("[Thread3] APP_MODE_FFT_VERIFY=1: FFT verification mode\n");

    /* Match normal camera path which disables D-cache; avoids stale reads in mmap regions. */
    __DSB();
    __DMB();
    SCB_CleanDCache();
    SCB_InvalidateDCache();
    SCB_DisableDCache();
    __DSB();
    __ISB();

    fsp_err_t err = hyperram_init();
    if (FSP_SUCCESS != err)
    {
        xprintf("[Thread3] HyperRAM init error\n");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

#if APP_MODE_FFT_VERIFY_RUN_FFT256
    {
        int runs = (int)APP_MODE_FFT_VERIFY_FFT256_RUNS;
        if (runs <= 0)
        {
            runs = 1;
        }

        for (int run = 1; run <= runs; run++)
        {
            xprintf("\n[Thread3] Test 6 repeat %d/%d\n", run, runs);
            fft_test_hyperram_256x256();
        }
    }
#elif APP_MODE_FFT_VERIFY_RUN_FFT128
    fft_test_hyperram_128x128();
#else
    fft_depth_test_all();
#endif

    xprintf("[Thread3] FFT verification complete\n");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif

    // OctalRAMテスト
    vTaskDelay(pdMS_TO_TICKS(3000));
    // char test_data[16] = "DEADBEEF1234567\0";
    // char read_data[16] = {0};

    // for (int i = 0; i < 65536; i += 16)
    // {
    //     hyperram_b_write(&test_data[0], (void *)i, sizeof(test_data));
    // }

    // for (int i = 0; i < 65536; i += 16)
    // {
    //     read_data[0] = '\0';
    //     hyperram_b_read(&read_data[0], (void *)i, sizeof(test_data));
    //     xprintf("[Thread3] HyperRAM test read at offset %d: %s\n", i, read_data);
    //     vTaskDelay(pdMS_TO_TICKS(10));
    // }

    // hyperram_b_read(read_data, (void *)HYPERRAM_BASE_ADDR, sizeof(test_data));

    xprintf("[Thread3] PQ128 mode: generating p/q (int16) in HyperRAM\n");
    xprintf("[Thread3] ROI: %dx%d at (%d,%d) from Y (UYVY_SWAP_Y + 4px reorder)\n",
            (int)PQ128_SIZE, (int)PQ128_SIZE, (int)PQ128_X0, (int)PQ128_Y0);

    uint32_t last_seq = 0;
    while (1)
    {
        uint32_t seq = g_video_frame_seq;
        if (seq == 0 || seq == last_seq)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        uint32_t frame_base = (uint32_t)g_video_frame_base_offset;
        pq128_compute_and_store(frame_base, seq);

#if ENABLE_FC128_DEPTH
        fc128_compute_depth_and_store(frame_base, seq);
#endif
        last_seq = seq;

        /* Yield; Thread0 capture interval is currently 500ms. */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}