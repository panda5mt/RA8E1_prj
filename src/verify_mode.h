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
#define APP_MODE_FFT_VERIFY_VERBOSE 1
#endif

#ifndef APP_MODE_FFT_VERIFY_USE_DELAYS
#define APP_MODE_FFT_VERIFY_USE_DELAYS 1
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
