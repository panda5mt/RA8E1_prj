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
#define APP_MODE_FFT_VERIFY_RUN_FFT128 1
#endif
