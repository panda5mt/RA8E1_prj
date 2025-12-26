#include "fft_depth_test.h"
#include "putchar_ra8usb.h"
#include <math.h>
#include <string.h>

// Helium MVE (ARM M-profile Vector Extension) support
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE > 0)
#include <arm_mve.h>
#define USE_HELIUM_MVE 1
#else
#define USE_HELIUM_MVE 0
#endif

/* 三角関数テーブル（事前計算で高速化） */
#define MAX_FFT_SIZE 256
static float cos_table[MAX_FFT_SIZE / 2];
static float sin_table[MAX_FFT_SIZE / 2];
static bool trig_table_initialized = false;

/* スタティックバッファ（動的メモリ割り当て回避） */
static float g_fft_buffer_real[FFT_TEST_POINTS];
static float g_fft_buffer_imag[FFT_TEST_POINTS];
static float g_fft_buffer_original[FFT_TEST_POINTS];
static float g_fft_col_real[FFT_TEST_SIZE];
static float g_fft_col_imag[FFT_TEST_SIZE];

/* 三角関数テーブルの初期化 */
static void init_trig_tables(int N)
{
    if (trig_table_initialized && N <= MAX_FFT_SIZE)
    {
        return;
    }

    for (int i = 0; i < N / 2; i++)
    {
        float angle = 2.0f * M_PI * i / N;
        cos_table[i] = cosf(angle);
        sin_table[i] = sinf(angle);
    }

    trig_table_initialized = true;
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

/* 1D FFT (Danielson-Lanczos法、MVE最適化版) */
void fft_1d_mve(float *real, float *imag, int N, bool is_inverse)
{
    // 三角関数テーブル初期化
    init_trig_tables(N);

    // log2(N)を計算
    int log2n = 0;
    int temp = N;
    while (temp > 1)
    {
        temp >>= 1;
        log2n++;
    }

    // ビット反転並び替え
    for (int i = 0; i < N; i++)
    {
        int j = bit_reverse(i, log2n);
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

        for (int k = 0; k < N; k += step)
        {
#if USE_HELIUM_MVE
            // MVE版: 4複素数並列処理
            int m;
            for (m = 0; m < halfStep - 3; m += 4)
            {
                int idx[4];
                for (int v = 0; v < 4; v++)
                {
                    idx[v] = (m + v) * tableStep % (N / 2);
                }

                // 回転係数（twiddle factors）
                float w_real[4], w_imag[4];
                for (int v = 0; v < 4; v++)
                {
                    w_real[v] = cos_table[idx[v]];
                    w_imag[v] = is_inverse ? sin_table[idx[v]] : -sin_table[idx[v]];
                }

                // バタフライ演算（4並列）
                for (int v = 0; v < 4; v++)
                {
                    int i = k + m + v;
                    int j = i + halfStep;

                    // t = w * x[j]
                    float t_real = w_real[v] * real[j] - w_imag[v] * imag[j];
                    float t_imag = w_real[v] * imag[j] + w_imag[v] * real[j];

                    // バタフライ更新
                    real[j] = real[i] - t_real;
                    imag[j] = imag[i] - t_imag;
                    real[i] += t_real;
                    imag[i] += t_imag;
                }
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
                int index = m * tableStep % (N / 2);
                float w_real = cos_table[index];
                float w_imag = is_inverse ? sin_table[index] : -sin_table[index];

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
        float scale = 1.0f / N;
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

    // 列方向にFFT（スタティックバッファ使用）
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

/* 行列表示（デバッグ用） */
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

/* RMSE計算（精度評価） */
float fft_calculate_rmse(float *data1, float *data2, int size)
{
    float sum_sq_error = 0.0f;

    for (int i = 0; i < size; i++)
    {
        float diff = data1[i] - data2[i];
        sum_sq_error += diff * diff;
    }

    return sqrtf(sum_sq_error / size);
}

/* テスト1: インパルス応答テスト */
void fft_test_impulse(void)
{
    xprintf("\n========== FFT Test 1: Impulse Response ==========\n");

    // スタティックバッファ使用
    float *real = g_fft_buffer_real;
    float *imag = g_fft_buffer_imag;

    // インパルス信号（中央に1、他は0）
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

    // DC成分確認（中央にインパルス→位相シフト→市松模様）
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

    // 正弦波パターン（2周期）
    memset(imag, 0, FFT_TEST_POINTS * sizeof(float));
    for (int r = 0; r < FFT_TEST_SIZE; r++)
    {
        for (int c = 0; c < FFT_TEST_SIZE; c++)
        {
            float angle = 2.0f * M_PI * 2.0f * c / FFT_TEST_SIZE;
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

/* テスト3: FFT→IFFT往復テスト（精度検証） */
void fft_test_round_trip(void)
{
    xprintf("\n========== FFT Test 3: Round Trip (FFT -> IFFT) ==========\n");

    // スタティックバッファ使用
    float *real = g_fft_buffer_real;
    float *imag = g_fft_buffer_imag;
    float *original = g_fft_buffer_original;

    // ランダムパターン（疑似乱数）
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

    // 精度評価（RMSE計算）
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

    fft_test_impulse();
    vTaskDelay(pdMS_TO_TICKS(500));

    fft_test_sine_wave();
    vTaskDelay(pdMS_TO_TICKS(500));

    fft_test_round_trip();

    xprintf("\n========================================\n");
    xprintf("  All FFT Tests Complete\n");
    xprintf("========================================\n\n");
}
