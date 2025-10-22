#include "putchar_ra8usb.h"
#include "r_ospi_b.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define HYPERRAM_BASE_ADDR ((void *)0x90000000U) /* Device on CS1 */
// COMMAND SET(infineon S80KS5123)
// #define  <COMMAND>               <CODE>   <CA-DATA> | <ADDRESS(bytes)>   | <Latency cycles>  | <Data (bytes)>
#define OSPI_B_COMMAND_RESET_ENABLE (0x6666)   // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_RESET (0x9999)          // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_READ_ID (0x9F9F)        // 8-8-8  |  0x00(4bytes)      |  3-7              |  (4bytes)
#define OSPI_B_COMMAND_POWER_DOWN (0xB9B9)     // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_READ (0xEEEE)           // 8-8-8  |  (4bytes)          |  3-7              |  1 to \infty
#define OSPI_B_COMMAND_WRITE (0xDEDE)          // 8-8-8  |  (4bytes)          |  3-7              |  1 to \infty
#define OSPI_B_COMMAND_WRITE_ENABLE (0x0606)   // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_WRITE_DISABLE (0x0404)  // 8-0-0  |  0                 |  0                |  0
#define OSPI_B_COMMAND_READ_REGISTER (0x6565)  // 8-8-8  |  (4bytes)          |  3-7              |  (2bytes)
#define OSPI_B_COMMAND_WRITE_REGISTER (0x7171) // 8-8-8  |  (4bytes)          |  0                |  (2bytes)

#define OSPI_RAM_LATENCY_CYCLES (8U)
#define OSPI_RAM_COMMAND_BYTES (1U)

    extern bool ospi_b_dma_sent;
    extern spi_flash_direct_transfer_t g_ospi0_trans;
    extern ospi_b_xspi_command_set_t g_command_sets[];
    fsp_err_t hyperram_init(void);
    fsp_err_t ospi_raw_trans(spi_flash_direct_transfer_t *p_trans,
                             uint16_t command, uint8_t cmd_len,
                             uint32_t address, uint8_t addr_len,
                             uint32_t data, uint8_t data_len,
                             uint8_t dummy_cycle, spi_flash_direct_transfer_dir_t dir);

    void ospi_dump_regs(void);
    void ospi_wait_mmap_idle(void);
    void dump_ospi_read_side(R_XSPI0_Type *r, int ch);
    fsp_err_t hyperram_b_write(const void *p_src, void *p_dest, uint32_t total_length);

#ifdef __cplusplus
}
#endif