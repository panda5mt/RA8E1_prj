#include "main_thread0.h"
#include "hal_data.h"
#include "projdefs.h"
#include "r_ceu.h"
#include "xprintf.h"
#include "cam.h"

// #define VGA_WIDTH (256)
// #define VGA_HEIGHT (256)
// #define BYTE_PER_PIXEL (2)

void putchar_ra8usb(uint8_t c);
/* Main Thread entry function */
/* pvParameters contains TaskHandle_t */

void main_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    //  init UART & printf
    xdev_out(putchar_ra8usb);
    // init DVP camera
    cam_init(DEV_OV3640);
    // capture from camera
    vTaskDelay(pdMS_TO_TICKS(200));
    cam_capture();
    vTaskDelay(pdMS_TO_TICKS(200));
    cam_capture();
    cam_close();
    xprintf("!srt\n");
    // cast pointer
    uint32_t *image_p32 = (uint32_t *)g_image_qvga_sram;

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL / 4; i++)
    {

        xprintf("0x%08X\n",
                image_p32[i]);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void putchar_ra8usb(uint8_t c)
{

    static bool skip = false;
    if (skip == true)
        return;
    uint8_t p[2];
    p[0] = c;
    p[1] = '\0';

    // skip if usb is not active
    if (xQueueSend(xQueueMes, p, pdMS_TO_TICKS(1000)) != pdPASS)
    {
        skip = true;
    }
    return;
}
