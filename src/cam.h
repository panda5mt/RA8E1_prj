#ifndef CAM_H_
#define CAM_H_

#include "hal_data.h"
#include "r_ceu.h"
#include "sccb_if.h"

#define VGA_WIDTH (256)
#define VGA_HEIGHT (256)
#define BYTE_PER_PIXEL (2)

extern uint8_t g_image_qvga_sram[VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL] /*BSP_ALIGN_VARIABLE(8)*/;

extern void g_ceu0_user_callback(capture_callback_args_t *p_args);
void cam_init(void);

#endif