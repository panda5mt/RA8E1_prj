/*
 * Copyright (c) 2020 - 2024 Renesas Electronics Corporation and/or its affiliates
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "blinky_thread.h"
#include <stdio.h>
#include <string.h>

extern bsp_leds_t g_bsp_leds;

/* Blinky Thread entry function */
void blinky_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* LED type structure */
    bsp_leds_t leds = g_bsp_leds;

    /* If this board has no LEDs then trap here */
    if (0 == leds.led_count)
    {
        while (1)
        {
            ; // There are no LEDs on this board
        }
    }

    /* Holds level to set for pins */
    bsp_io_level_t pin_level = BSP_IO_LEVEL_LOW;

    R_SCI_B_UART_Open(&g_uart0_ctrl, &g_uart0_cfg);
    while (1)
    {
        /* Enable access to the PFS registers. If using r_ioport module then register protection is automatically
         * handled. This code uses BSP IO functions to show how it is used.
         */
        R_BSP_PinAccessEnable();

        uint8_t data[256];
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        sprintf((char *)data, "time:%u[ms]\n", (unsigned int)uptime_ms); // sprintfにはchar*が必要なためキャスト
        R_SCI_B_UART_Write(&g_uart0_ctrl, data, strlen((char *)data));   // strlenもchar*を要求するのでキャスト

        /* Update all board LEDs */
        for (uint32_t i = 0; i < leds.led_count; i++)
        {
            /* Get pin to toggle */
            uint32_t pin = leds.p_leds[i];

            /* Write to this pin */
            R_BSP_PinWrite((bsp_io_port_pin_t)pin, pin_level);
        }

        /* Protect PFS registers */
        R_BSP_PinAccessDisable();

        /* Toggle level for next write */
        if (BSP_IO_LEVEL_LOW == pin_level)
        {
            pin_level = BSP_IO_LEVEL_HIGH;
        }
        else
        {
            pin_level = BSP_IO_LEVEL_LOW;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
