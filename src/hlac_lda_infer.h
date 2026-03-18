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

    /* ROI (Region Of Interest) variant: Compute HLAC from a rectangular block within a larger image.
     *
     * - img_addr: HyperRAM byte address of the first pixel of the full image (row-major)
     * - img_stride: width of the full image in pixels (used for row offset calculation)
     * - x0, y0: top-left corner of the ROI block (in pixels)
     * - block_w, block_h: width and height of the block in pixels
     * - out25: output features (length 25)
     *
     * Useful for block-wise HLAC extraction when inferring over a tiled grid.
     * Requires: y0+block_h <= img_height and x0+block_w <= img_width (no bounds checking).
     */
    void hlac25_compute_from_u8_hyperram_roi(uint32_t img_addr, uint32_t img_stride,
                                             uint32_t x0, uint32_t y0,
                                             uint32_t block_w, uint32_t block_h,
                                             float out25[25]);

    /* LDA predict: returns label in [0..num_classes-1], or -1 on error.
     * If out_best_score is non-NULL, stores the best score.
     */
    int hlac_lda_predict(const float feats25[25], float *out_best_score);

    /* Extended predictor.
     *
     * - compute_softmax_prob != 0 and out_best_prob != NULL:
     *     computes softmax probability of the best class.
     * - otherwise:
     *     skips softmax to reduce CPU load.
     *
     * Returns label in [0..num_classes-1], or -1 on error.
     */
    int hlac_lda_predict_ex(const float feats25[25],
                            float *out_best_score,
                            float *out_best_prob,
                            int compute_softmax_prob);

#ifdef __cplusplus
}
#endif
