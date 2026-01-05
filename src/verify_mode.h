#pragma once

/*
 * Verification mode switch.
 *
 * Set APP_MODE_FFT_VERIFY to 1 to:
 * - Stop camera capture in thread0
 * - Stop UDP/LwIP in thread1
 * - Run FFT verification in thread3
 *
 * Default is 0 (normal camera + UDP operation).
 */
#ifndef APP_MODE_FFT_VERIFY
#define APP_MODE_FFT_VERIFY 1
#endif

/* When verification mode is enabled, run 128x128 FFT test in thread3. */
#ifndef APP_MODE_FFT_VERIFY_RUN_FFT128
#define APP_MODE_FFT_VERIFY_RUN_FFT128 0
#endif

/* When verification mode is enabled, optionally run 256x256 FFT test in thread3. */
#ifndef APP_MODE_FFT_VERIFY_RUN_FFT256
#define APP_MODE_FFT_VERIFY_RUN_FFT256 1
#endif

/* When running the 256x256 FFT test (Test 6), repeat it this many times. */
#ifndef APP_MODE_FFT_VERIFY_FFT256_RUNS
#define APP_MODE_FFT_VERIFY_FFT256_RUNS 10
#endif

/*
 * Reduce the number of data patterns per test to speed up verification.
 * Patterns are selected from the built-in list of 5 patterns.
 * - COUNT: how many patterns to run (1..5)
 * - START: start index (0..4), subsequent patterns wrap around
 */
#ifndef APP_MODE_FFT_VERIFY_FFT256_PATTERN_COUNT
#define APP_MODE_FFT_VERIFY_FFT256_PATTERN_COUNT 1
#endif

#ifndef APP_MODE_FFT_VERIFY_FFT256_PATTERN_START
#define APP_MODE_FFT_VERIFY_FFT256_PATTERN_START 3
#endif

#ifndef APP_MODE_FFT_VERIFY_FFT128_PATTERN_COUNT
#define APP_MODE_FFT_VERIFY_FFT128_PATTERN_COUNT 1
#endif

#ifndef APP_MODE_FFT_VERIFY_FFT128_PATTERN_START
#define APP_MODE_FFT_VERIFY_FFT128_PATTERN_START 3
#endif

/*
 * Print phase breakdown (row FFT / transpose / col FFT / transpose) once.
 * Useful to identify bottlenecks while keeping VERBOSE=0.
 */
#ifndef APP_MODE_FFT_VERIFY_PRINT_PHASES_ONCE
#define APP_MODE_FFT_VERIFY_PRINT_PHASES_ONCE 1
#endif

/*
 * When verification mode is enabled, select the 128x128 2D FFT implementation:
 * - 0: BLOCKED (32x32 blocks; good for round-trip sanity)
 * - 1: FULL (true 128x128 spectrum via HyperRAM transpose)
 */
#ifndef APP_MODE_FFT_VERIFY_USE_FFT128_FULL
#define APP_MODE_FFT_VERIFY_USE_FFT128_FULL 0
#endif

/*
 * Verification-mode logging/pace controls.
 * - Set VERBOSE=0 to reduce UART/USB CDC log traffic.
 * - Set USE_DELAYS=0 to remove vTaskDelay() pacing in test loops.
 *   (Useful for performance measurement. If HyperRAM becomes unstable,
 *    restore delays or reduce bus contention.)
 */
#ifndef APP_MODE_FFT_VERIFY_VERBOSE
#define APP_MODE_FFT_VERIFY_VERBOSE 0
#endif

#ifndef APP_MODE_FFT_VERIFY_USE_DELAYS
#define APP_MODE_FFT_VERIFY_USE_DELAYS 0
#endif

/* Delay values (ms) used when APP_MODE_FFT_VERIFY_USE_DELAYS=1 */
#ifndef APP_MODE_FFT_VERIFY_ROW_DELAY_MS
#define APP_MODE_FFT_VERIFY_ROW_DELAY_MS 2
#endif

#ifndef APP_MODE_FFT_VERIFY_PHASE_DELAY_MS
#define APP_MODE_FFT_VERIFY_PHASE_DELAY_MS 10
#endif

#ifndef APP_MODE_FFT_VERIFY_ITER_DELAY_MS
#define APP_MODE_FFT_VERIFY_ITER_DELAY_MS 50
#endif

/*
 * Tile size used by hyperram_transpose_tiled() in the FULL 2D FFT.
 * Typical options: 32 or 64.
 * - Larger tiles reduce transaction overhead (often faster)
 * - Larger tiles use more SRAM for the tile buffers
 */
#ifndef APP_MODE_FFT_TRANSPOSE_TILE
#define APP_MODE_FFT_TRANSPOSE_TILE 64
#endif
