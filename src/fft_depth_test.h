#ifndef FFT_DEPTH_TEST_H
#define FFT_DEPTH_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include "hal_data.h"
#include "hyperram_integ.h"

/* 2D FFT/IFFT テスト用定義 */
#define FFT_TEST_SIZE 16 // 16x16 = 256ポイント
#define FFT_TEST_POINTS (FFT_TEST_SIZE * FFT_TEST_SIZE)

/* HyperRAM上のFFTテスト用バッファオフセット（8MB中の空き領域を使用） */
// Multigrid: 0x05DD00～0x18A000 (1.2MB)なので、2MB位置は安全
#define FFT_TEST_OFFSET 0x200000 // 2MB位置から開始（Multigrid領域の後）
#define FFT_REAL_OFFSET (FFT_TEST_OFFSET)
#define FFT_IMAG_OFFSET (FFT_TEST_OFFSET + FFT_TEST_POINTS * sizeof(float))
#define FFT_ORIGINAL_OFFSET (FFT_TEST_OFFSET + FFT_TEST_POINTS * sizeof(float) * 2)
#define FFT_COL_REAL_OFFSET (FFT_TEST_OFFSET + FFT_TEST_POINTS * sizeof(float) * 3)
#define FFT_COL_IMAG_OFFSET (FFT_TEST_OFFSET + FFT_TEST_POINTS * sizeof(float) * 3 + FFT_TEST_SIZE * sizeof(float))

/* テスト関数 */
void fft_depth_test_all(void);

/* 個別テスト関数 */
void fft_test_impulse(void);             // インパルス応答テスト
void fft_test_sine_wave(void);           // 正弦波テスト
void fft_test_round_trip(void);          // FFT→IFFT往復テスト
void fft_test_hyperram_round_trip(void); // HyperRAMベースFFT→IFFT往復テスト

/* 1D FFT/IFFT (MVE最適化版) */
void fft_1d_mve(float *real, float *imag, int N, bool is_inverse);

/* 2D FFT/IFFT */
void fft_2d(float *real, float *imag, int rows, int cols, bool is_inverse);

/* HyperRAMベース2D FFT/IFFT（メモリ効率版） */
void fft_2d_hyperram(
    uint32_t hyperram_input_real_offset,
    uint32_t hyperram_input_imag_offset,
    uint32_t hyperram_output_real_offset,
    uint32_t hyperram_output_imag_offset,
    int rows, int cols, bool is_inverse);

/* ユーティリティ */
void fft_print_matrix(const char *label, float *data, int rows, int cols, int max_display);
float fft_calculate_rmse(float *data1, float *data2, int size);

#endif // FFT_DEPTH_TEST_H
