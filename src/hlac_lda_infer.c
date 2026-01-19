#include "hlac_lda_infer.h"

#include "hlac_lda_model.h"
#include "hyperram_integ.h"

#include <string.h>
#include <math.h>
#include <stdbool.h>

#ifndef HLAC_MAX_IMAGE_W
#define HLAC_MAX_IMAGE_W (320U)
#endif

/* Offsets order must match matlab/extract_hlac_features.m:
 * 1: (-1,-1) 2: (-1,0) 3: (-1,1) 4: (0,-1) 5: (0,1) 6:(1,-1) 7:(1,0) 8:(1,1)
 */
static const int8_t k_offsets8[8][2] = {
    {-1, -1},
    {-1, 0},
    {-1, 1},
    {0, -1},
    {0, 1},
    {1, -1},
    {1, 0},
    {1, 1},
};

static const int8_t k_d4[8][2][2] = {
    {{1, 0}, {0, 1}},   /* identity */
    {{0, -1}, {1, 0}},  /* rot90 */
    {{-1, 0}, {0, -1}}, /* rot180 */
    {{0, 1}, {-1, 0}},  /* rot270 */
    {{-1, 0}, {0, 1}},  /* reflect y-axis */
    {{1, 0}, {0, -1}},  /* reflect x-axis */
    {{0, 1}, {1, 0}},   /* reflect diagonal */
    {{0, -1}, {-1, 0}}, /* reflect other diagonal */
};

static inline void apply_T(const int8_t T[2][2], const int8_t v[2], int8_t out[2])
{
    out[0] = (int8_t)(T[0][0] * v[0] + T[0][1] * v[1]);
    out[1] = (int8_t)(T[1][0] * v[0] + T[1][1] * v[1]);
}

static inline bool lex_gt_vec(const int8_t a[2], const int8_t b[2])
{
    return (a[0] > b[0]) || ((a[0] == b[0]) && (a[1] > b[1]));
}

static inline bool lex_lt_key4(const int8_t a[4], const int8_t b[4])
{
    for (int i = 0; i < 4; i++)
    {
        if (a[i] < b[i])
            return true;
        if (a[i] > b[i])
            return false;
    }
    return false;
}

static inline bool eq_key4(const int8_t a[4], const int8_t b[4])
{
    return (a[0] == b[0]) && (a[1] == b[1]) && (a[2] == b[2]) && (a[3] == b[3]);
}

static int8_t s_pair_idx[20][2];
static bool s_pairs_inited = false;

static void hlac25_init_pairs_once(void)
{
    if (s_pairs_inited)
    {
        return;
    }

    /* Enumerate all unordered pairs with repetition among 8 neighbors: 36 pairs */
    struct PairRec
    {
        int8_t i;
        int8_t j;
        int8_t key[4];
    } rec[36];
    int n = 0;
    for (int8_t i = 0; i < 8; i++)
    {
        for (int8_t j = i; j < 8; j++)
        {
            rec[n].i = i;
            rec[n].j = j;

            const int8_t *a = k_offsets8[i];
            const int8_t *b = k_offsets8[j];

            /* Find canonical D4 key = lexicographically smallest (dy1,dx1,dy2,dx2). */
            int8_t best[4] = {127, 127, 127, 127};
            for (int t = 0; t < 8; t++)
            {
                int8_t a2[2];
                int8_t b2[2];
                apply_T(k_d4[t], a, a2);
                apply_T(k_d4[t], b, b2);

                if (lex_gt_vec(a2, b2))
                {
                    int8_t tmp0 = a2[0];
                    int8_t tmp1 = a2[1];
                    a2[0] = b2[0];
                    a2[1] = b2[1];
                    b2[0] = tmp0;
                    b2[1] = tmp1;
                }

                int8_t key[4] = {a2[0], a2[1], b2[0], b2[1]};
                if (lex_lt_key4(key, best))
                {
                    memcpy(best, key, sizeof(best));
                }
            }

            memcpy(rec[n].key, best, sizeof(rec[n].key));
            n++;
        }
    }

    /* Sort by key (stable enough for our 36 items). */
    for (int i = 1; i < n; i++)
    {
        struct PairRec v = rec[i];
        int j = i - 1;
        while (j >= 0 && lex_lt_key4(v.key, rec[j].key))
        {
            rec[j + 1] = rec[j];
            j--;
        }
        rec[j + 1] = v;
    }

    /* Pick first representative per unique key -> should yield 20 patterns. */
    int out_n = 0;
    int8_t last_key[4] = {127, 127, 127, 127};
    for (int i = 0; i < n; i++)
    {
        if (out_n == 0 || !eq_key4(rec[i].key, last_key))
        {
            if (out_n < 20)
            {
                s_pair_idx[out_n][0] = rec[i].i;
                s_pair_idx[out_n][1] = rec[i].j;
                memcpy(last_key, rec[i].key, sizeof(last_key));
                out_n++;
            }
        }
    }

    /* If something went wrong, fall back to a deterministic subset. */
    if (out_n != 20)
    {
        int k = 0;
        for (int8_t i = 0; i < 8 && k < 20; i++)
        {
            for (int8_t j = i; j < 8 && k < 20; j++)
            {
                s_pair_idx[k][0] = i;
                s_pair_idx[k][1] = j;
                k++;
            }
        }
    }

    s_pairs_inited = true;
}

void hlac25_compute_from_u8_hyperram(uint32_t img_addr, uint32_t width, uint32_t height, float out25[25])
{
    if (!out25 || width == 0U || height == 0U || width > HLAC_MAX_IMAGE_W)
    {
        return;
    }

    hlac25_init_pairs_once();

    memset(out25, 0, 25U * sizeof(float));

    const float inv255 = 1.0f / 255.0f;

    uint8_t prev[HLAC_MAX_IMAGE_W];
    uint8_t cur[HLAC_MAX_IMAGE_W];
    uint8_t next[HLAC_MAX_IMAGE_W];

    memset(prev, 0, width);
    memset(cur, 0, width);
    memset(next, 0, width);

    /* Preload cur (y=0) and next (y=1). */
    (void)hyperram_b_read(cur, (void *)(img_addr + 0U), width);
    if (height > 1U)
    {
        (void)hyperram_b_read(next, (void *)(img_addr + width), width);
    }

    const uint32_t count = width * height;

    /* Neighbor indices (0-based) corresponding to MATLAB: right, down, rd, ru */
    const int idx_right = 4; /* (0,1) */
    const int idx_down = 6;  /* (1,0) */
    const int idx_rd = 7;    /* (1,1) */
    const int idx_ru = 2;    /* (-1,1) */

    float neigh[8];

    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            const float center = (float)cur[x] * inv255;

            /* Build 8-neighborhood with zero padding. */
            for (int k = 0; k < 8; k++)
            {
                int yy = (int)y + (int)k_offsets8[k][0];
                int xx = (int)x + (int)k_offsets8[k][1];
                if (xx < 0 || xx >= (int)width)
                {
                    neigh[k] = 0.0f;
                    continue;
                }
                if (yy < 0)
                {
                    neigh[k] = 0.0f;
                }
                else if (yy == (int)y - 1)
                {
                    neigh[k] = (float)prev[(uint32_t)xx] * inv255;
                }
                else if (yy == (int)y)
                {
                    neigh[k] = (float)cur[(uint32_t)xx] * inv255;
                }
                else if (yy == (int)y + 1)
                {
                    neigh[k] = (float)next[(uint32_t)xx] * inv255;
                }
                else
                {
                    /* Only 3x3 neighborhood is used; yy can only differ by +/-1. */
                    neigh[k] = 0.0f;
                }
            }

            out25[0] += center;
            out25[1] += center * neigh[idx_right];
            out25[2] += center * neigh[idx_down];
            out25[3] += center * neigh[idx_rd];
            out25[4] += center * neigh[idx_ru];

            for (int p = 0; p < 20; p++)
            {
                int i = (int)s_pair_idx[p][0];
                int j = (int)s_pair_idx[p][1];
                out25[5 + p] += center * neigh[i] * neigh[j];
            }
        }

        /* Advance ring buffers. */
        memcpy(prev, cur, width);
        memcpy(cur, next, width);
        if (y + 2U < height)
        {
            (void)hyperram_b_read(next, (void *)(img_addr + (y + 2U) * width), width);
        }
        else
        {
            memset(next, 0, width);
        }
    }

    if (count != 0U)
    {
        const float inv_count = 1.0f / (float)count;
        for (int i = 0; i < 25; i++)
        {
            out25[i] *= inv_count;
        }
    }
}

int hlac_lda_predict(const float feats25[25], float *out_best_score)
{
    if (!feats25)
    {
        return -1;
    }

    uint32_t C = g_hlac_lda_num_classes;
    if (C == 0U)
    {
        return -1;
    }
    if (C > HLAC_MAX_CLASSES)
    {
        C = HLAC_MAX_CLASSES;
    }

    int best = 0;
    float best_score = -INFINITY;

    for (uint32_t c = 0; c < C; c++)
    {
        float s = g_hlac_lda_b[c];
        for (uint32_t i = 0; i < 25U; i++)
        {
            float z = feats25[i];
            float sig = g_hlac_feature_std[i];
            if (fabsf(sig) < 1e-12f)
            {
                sig = 1.0f;
            }
            z = (z - g_hlac_feature_mean[i]) / sig;
            s += g_hlac_lda_W[i][c] * z;
        }

        if (s > best_score)
        {
            best_score = s;
            best = (int)c;
        }
    }

    if (out_best_score)
    {
        *out_best_score = best_score;
    }
    return best;
}
