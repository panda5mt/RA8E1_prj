#include "main_thread0.h"
#include "hal_data.h"
#include "r_ceu.h"
#include "xprintf_helper.h"
#include "cam.h"

#define VGA_WIDTH (256)
#define VGA_HEIGHT (256)
#define BYTE_PER_PIXEL (2)

/* Main Thread entry function */
/* pvParameters contains TaskHandle_t */
void main_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    //  init UART & printf
    xdev_out(put_char_ra8);

    // init DVP camera
    cam_init();
    cam_capture();
    cam_close();
    cam_init();
    vTaskDelay(pdMS_TO_TICKS(200));
    cam_capture();
    // cam_close();

    xprintf("!srt\n");
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL; i += 4)
    {

        xprintf("0x%02X%02X%02X%02X\n",
                g_image_qvga_sram[i],
                g_image_qvga_sram[i + 1],
                g_image_qvga_sram[i + 2],
                g_image_qvga_sram[i + 3]);
    }

    /* TODO: add your own code here */
    while (1)
    {
        uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        xprintf("upset time = %d[msec]\n", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
