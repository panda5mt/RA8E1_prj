#include "putchar_ra8usb.h"
#include "r_ospi_b.h"

#define HYPERRAM_BASE_ADDR ((void *)0x90000000U) /* Device on CS1 */

extern ospi_b_xspi_command_set_t g_command_sets[];
fsp_err_t hyperram_init(void);
