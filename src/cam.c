#include "hal_data.h"
#include "r_ceu.h"
#include "xprintf_helper.h"
#include "sccb_if.h"
#include "cam.h"

bool g_ceu_capture_error;
bool g_ceu_capture_complete;
uint8_t g_image_qvga_sram[VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL] /*BSP_ALIGN_VARIABLE(8)*/;
// callback function
void g_ceu0_user_callback(capture_callback_args_t *p_args)
{
    /* Multiple event flags may be set simultaneously */
    if (p_args->event & (uint32_t) ~(CEU_EVENT_HD | CEU_EVENT_VD | CEU_EVENT_FRAME_END))
    {
        /* Error processing should occur first. Application should not process complete event if error occurred. */
        g_ceu_capture_error = true;
    }
    else
    {
        if (p_args->event & CEU_EVENT_VD)
        {
            /* Capture has started. Process V-Sync event. */
        }
        if (p_args->event & CEU_EVENT_FRAME_END)
        {
            /* Capture is complete and no error has occurred */
            g_ceu_capture_complete = true;
        }
    }
}

void cam_init(void)
{

    // init I2C and PWM
    sccb_and_clk_init();

    ////////////////////// CAMERA START
    fsp_err_t err = FSP_SUCCESS;

    R_CEU_Open(&g_ceu0_ctrl, &g_ceu0_cfg);
    g_ceu_capture_error = false;
    g_ceu_capture_complete = false;

    // R_BSP_MODULE_START(FSP_IP_CEC, 0);
    err = R_CEU_CaptureStart(&g_ceu0_ctrl, g_image_qvga_sram);
    assert(FSP_SUCCESS == err);
    xprintf("[Camera Capture] Start.\n");

    while (!g_ceu_capture_complete /*&& !g_ceu_capture_error*/)
    {
        /* Wait for capture to complete. */
        if (g_ceu_capture_error)
        {
            g_ceu_capture_error = false;
        }
    }

    xprintf("[Camera Capture] end\n");
    /* Process image here if capture was successful. */
    err = R_CEU_Close(&g_ceu0_ctrl);
    assert(FSP_SUCCESS == err);
    ////////////////////// CAMERA END
}