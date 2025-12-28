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
            // MVE版: 4複素数並列処理（真のSIMD命令使用）
            int m;
            for (m = 0; m < halfStep - 3; m += 4)
            {
                // 回転係数インデックス計算
                int idx[4];
                for (int v = 0; v < 4; v++)
                {
                    idx[v] = (m + v) * tableStep % (N / 2);
                }

                // 回転係数をベクトルにロード
                float w_real[4], w_imag[4];
                for (int v = 0; v < 4; v++)
                {
                    w_real[v] = cos_table[idx[v]];
                    w_imag[v] = is_inverse ? sin_table[idx[v]] : -sin_table[idx[v]];
                }

                float32x4_t w_real_vec = vld1q_f32(w_real);
                float32x4_t w_imag_vec = vld1q_f32(w_imag);

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

/* HyperRAMベース2D FFT/IFFT（メモリ効率版） */
void fft_2d_hyperram(
    uint32_t hyperram_input_real_offset,
    uint32_t hyperram_input_imag_offset,
    uint32_t hyperram_output_real_offset,
    uint32_t hyperram_output_imag_offset,
    int rows, int cols, bool is_inverse)
{
    // RAM上に1行/1列分の作業バッファのみ確保（最大256要素）
    static float work_real[256];
    static float work_imag[256];
    static float col_real[256];
    static float col_imag[256];

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

        // 進捗表示（10行ごと）
        if ((r + 1) % 10 == 0 || r == rows - 1)
        {
            xprintf("[FFT-HyperRAM] Processing rows (%d/%d)...\r", r + 1, rows);
        }
    }
    xprintf("\n");

    // ========== 列方向FFT（転置方式） ==========
    static float transposed_real[256];
    static float transposed_imag[256];

    if ((rows * cols) > 256)
    {
        xprintf("[FFT] ERROR: Size exceeds 256 limit for transpose\n");
        return;
    }

    // 行優先で読み込み、列優先に転置
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

    // 列ごとにFFT実行（転置済みなので連続メモリアクセス）
    for (int c = 0; c < cols; c++)
    {
        fft_1d_mve(&transposed_real[c * rows], &transposed_imag[c * rows], rows, is_inverse);

        if ((c + 1) % 10 == 0 || c == cols - 1)
        {
            xprintf("[FFT-HyperRAM] Processing cols (%d/%d)...\r", c + 1, cols);
        }
    }
    xprintf("\n");

    // 逆転置して行優先に戻し、HyperRAMに書き込み
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

/* ROW処理のみ実行（デバッグ用） */
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

/* COL処理のみ実行（デバッグ用）- 転置方式 */
void fft_2d_hyperram_col_only(
    uint32_t hyperram_input_real_offset,
    uint32_t hyperram_input_imag_offset,
    uint32_t hyperram_output_real_offset,
    uint32_t hyperram_output_imag_offset,
    int rows, int cols, bool is_inverse)
{
    // 転置バッファ（RAM上に全データを一時保持）
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

    // 行優先で読み込み、列優先に転置
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

    // デバッグ: 転置直後の最初の列（16要素）を表示
    xprintf("[DEBUG] Transposed column 0 (before FFT):\n");
    for (int i = 0; i < 16; i++)
    {
        xprintf("  [%d] = %.6f\n", i, transposed_real[i]);
    }

    xprintf("[FFT-HyperRAM] Processing cols (0/%d)...\r", cols);

    // 列ごとにFFT実行（転置済みなので連続メモリアクセス）
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

    // 逆転置して行優先に戻し、HyperRAMに書き込み
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

/* テスト4: HyperRAMベースFFT→IFFT往復テスト（メモリ効率版） */
void fft_test_hyperram_round_trip(void)
{
    xprintf("\n========== FFT Test 4: HyperRAM-based Round Trip ==========\n");
    xprintf("[FFT] Testing memory-efficient HyperRAM implementation\n");
    xprintf("[FFT] Running 5 iterations with varying data patterns...\n");

    // 作業用バッファ（RAM上）
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

        // テストデータ生成（パターンを変化させる）
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

            // 32要素ごとにタスクスイッチを許可（ウォッチドッグ対策）
            if (i > 0 && (i % 32) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }

        // HyperRAMに入力データを書き込み
        hyperram_b_write(input_real, (void *)FFT_REAL_OFFSET, FFT_TEST_POINTS * sizeof(float));
        hyperram_b_write(input_imag, (void *)FFT_IMAG_OFFSET, FFT_TEST_POINTS * sizeof(float));

        // Forward FFT（HyperRAM経由、転置方式）
        uint32_t start = xTaskGetTickCount();
        fft_2d_hyperram(
            FFT_REAL_OFFSET,
            FFT_IMAG_OFFSET,
            FFT_REAL_OFFSET, // in-place変換
            FFT_IMAG_OFFSET,
            FFT_TEST_SIZE, FFT_TEST_SIZE, false);
        uint32_t mid = xTaskGetTickCount();
        forward_times[iter] = mid - start;

        // Inverse FFT（HyperRAM経由、転置方式）
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

        xprintf("  Forward: %u ms, Inverse: %u ms, RMSE: %.6e\n",
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
    xprintf("  RMSE:    avg=%.6e, min=%.6e, max=%.6e\n", rmse_avg, rmse_min, rmse_max);
    xprintf("  Forward: avg=%u ms\n", fwd_sum / NUM_ITERATIONS);
    xprintf("  Inverse: avg=%u ms\n", inv_sum / NUM_ITERATIONS);

    if (rmse_max < 1e-5f)
    {
        xprintf("[FFT] PASS: All iterations excellent precision (RMSE < 1e-5)\n");
        xprintf("[FFT] HyperRAM integration working correctly!\n");
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
    vTaskDelay(pdMS_TO_TICKS(200)); // Camera Captureの書き込みサイクル待ち

    fft_test_impulse();
    vTaskDelay(pdMS_TO_TICKS(500));

    fft_test_sine_wave();
    vTaskDelay(pdMS_TO_TICKS(500));

    fft_test_round_trip();
    vTaskDelay(pdMS_TO_TICKS(500));

    fft_test_hyperram_round_trip();

    xprintf("\n========================================\n");
    xprintf("  All FFT Tests Complete\n");
    xprintf("========================================\n\n");
}
