
#include <stdio.h>
#include <stdbool.h>
#include "hal_data.h"
#include "xprintf.h"

bool first_call = true;

void put_char_ra8(uint8_t ch)
{
    uint8_t *p;
    p = (uint8_t *)&ch;
    if (first_call == true)
    {
        R_SCI_B_UART_Open(&g_uart0_ctrl, &g_uart0_cfg);
        first_call = false;
    }
    R_SCI_B_UART_Write(&g_uart0_ctrl, p, sizeof(char));
}