#ifndef CAM_H_
#define CAM_H_

#include "hal_data.h"
#include "r_ceu.h"
#include "sccb_if.h"

#define VGA_WIDTH (320)
#define VGA_HEIGHT (240)
#define BYTE_PER_PIXEL (2)

extern uint8_t g_image_qvga_sram[VGA_WIDTH * VGA_HEIGHT * BYTE_PER_PIXEL] /* BSP_ALIGN_VARIABLE(8)*/;

extern void g_ceu0_user_callback(capture_callback_args_t *p_args);
void cam_init(camera_dev_t cam);
void cam_capture(void);
void cam_close(void);

#endif