#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * HLAC(25) + LDA model interface.
     *
     * This project intentionally does NOT track MATLAB training outputs in git.
     * Provide your trained parameters by generating/replacing hlac_lda_model.c.
     *
     * Default implementation (hlac_lda_model.c) is a stub (all zeros) so the
     * firmware builds out-of-the-box.
     */

#ifndef HLAC_FEATURE_DIM
#define HLAC_FEATURE_DIM (25U)
#endif

#ifndef HLAC_MAX_CLASSES
#define HLAC_MAX_CLASSES (10U)
#endif

    /* Actual number of classes used by the trained model (<= HLAC_MAX_CLASSES). */
    extern const uint32_t g_hlac_lda_num_classes;

    /* LDA parameters: score[c] = sum_i W[i][c] * z[i] + b[c] */
    extern const float g_hlac_lda_W[HLAC_FEATURE_DIM][HLAC_MAX_CLASSES];
    extern const float g_hlac_lda_b[HLAC_MAX_CLASSES];

    /* Optional standardization (recommended if used during training). */
    extern const float g_hlac_feature_mean[HLAC_FEATURE_DIM];
    extern const float g_hlac_feature_std[HLAC_FEATURE_DIM];

#ifdef __cplusplus
}
#endif
