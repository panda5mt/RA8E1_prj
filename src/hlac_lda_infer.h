#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Compute HLAC order=2 (25-dim) from an 8-bit image stored in HyperRAM.
     *
     * - img_addr: HyperRAM byte address of the first pixel (row-major, contiguous)
     * - width/height: image dimensions in pixels
     * - out25: output features (length 25)
     *
     * This matches matlab/extract_hlac_features.m when:
     * - input is already the desired HLAC image (e.g. |P|+|Q|), and
     * - use_sobel=false.
     */
    void hlac25_compute_from_u8_hyperram(uint32_t img_addr, uint32_t width, uint32_t height, float out25[25]);

    /* LDA predict: returns label in [0..num_classes-1], or -1 on error.
     * If out_best_score is non-NULL, stores the best score.
     */
    int hlac_lda_predict(const float feats25[25], float *out_best_score);

#ifdef __cplusplus
}
#endif
