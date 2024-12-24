#include "bsp_api.h"

#include "hal_data.h"
#include "xprintf_helper.h"

bool first_call = true;
void put_char_ra8(uint8_t ch)
{

    // R_SCI_B_UART_Writeが1文字送信が難しそうなので対策(fsp v.5.7.0)
    uint8_t p[2];
    p[0] = ch;
    p[1] = '\0';

    if (first_call == true)
    {
        R_BSP_MODULE_START(FSP_IP_SCI, 9);
        // init UART &printf
        R_SCI_B_UART_Open(&g_uart9_ctrl, &g_uart9_cfg);
        first_call = false;
    }

    uint32_t len = (uint32_t)(sizeof(p) / sizeof(uint8_t));

    R_SCI_B_UART_Write(&g_uart9_ctrl,
                       p,
                       (len == 1) ? len += 1 : len);
}