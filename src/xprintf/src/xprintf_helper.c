#include "bsp_api.h"

#include "hal_data.h"
#include "xprintf_helper.h"

void put_char_ra8(uint8_t ch)
{
    static bool first_call = true;
    uint8_t p[2];
    p[0] = ch;
    p[1] = '\0';
    if (first_call == true)
    {
        R_BSP_MODULE_START(FSP_IP_SCI, 9);
        // init UART & printf
        R_SCI_B_UART_Open(&g_uart9_ctrl, &g_uart9_cfg);
        first_call = false;
    }
    R_SCI_B_UART_Write(&g_uart9_ctrl, p, 2);
}