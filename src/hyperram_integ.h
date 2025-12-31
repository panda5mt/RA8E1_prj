#ifndef HYPERRAM_INTEG_H
#define HYPERRAM_INTEG_H

#include "putchar_ra8usb.h"
#include "r_ospi_b.h"
#include "FreeRTOS.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define HYPERRAM_BASE_ADDR ((void *)0x90000000U) /* Device on CS1 */
// COMMAND SET(ISSI IS66WVO8M8DALL)
// #define  <COMMAND>               <CODE>     <CA-DATA> | <ADDRESS(bytes)>     | <Latency cycles>  | <Data (bytes)>
#define OSPI_B_COMMAND_READ (0xA000)           // 8-8-8  |  (4bytes)            |  3-7              |  1 to \infty
#define OSPI_B_COMMAND_WRITE (0x2000)          // 8-8-8  |  (4bytes)            |  3-7              |  1 to \infty
#define OSPI_B_COMMAND_READ_REGISTER (0xC000)  // 8-8-8  |  0x0004_0000(4bytes) |  3-7              |  (2bytes)
#define OSPI_B_COMMAND_WRITE_REGISTER (0x4000) // 8-8-8  |  0x0004_0000(4bytes) |  0                |  (2bytes)
#define OSPI_B_COMMAND_READ_ID (0xC000)        // 8-8-8  |  0x0000_0000(4bytes) |  3-7              |  (2bytes)
#define OSPI_B_COMMAND_WRITE_ENABLE (0x00)     // NONE
#define OSPI_B_COMMAND_WRITE_DISABLE (0x00)    // NONE

#define OSPI_RAM_READ_LATENCY_CYCLES (8U)  /* Max latency cycles in DDR mode */
#define OSPI_RAM_WRITE_LATENCY_CYCLES (8U) /* Max latency cycles in DDR mode */

#define OSPI_RAM_COMMAND_BYTES (2U)

#define HYPERRAM_SIZE (8 * 1024 * 1024U) /* 8MB */

    extern bool ospi_b_dma_sent;
    extern spi_flash_direct_transfer_t g_ospi0_trans;
    extern ospi_b_xspi_command_set_t g_command_sets[];
    fsp_err_t hyperram_init(void);
    fsp_err_t hyperram_timing_optimization(void);
    fsp_err_t hyperram_rw_test(void);
    fsp_err_t ospi_raw_trans(spi_flash_direct_transfer_t *p_trans,
                             uint16_t command, uint8_t cmd_len,
                             uint32_t address, uint8_t addr_len,
                             uint32_t data, uint8_t data_len,
                             uint8_t dummy_cycle, spi_flash_direct_transfer_dir_t dir);

    void ospi_dump_regs(void);
    void ospi_wait_mmap_idle(void);
    void dump_ospi_read_side(R_XSPI0_Type *r, int ch);

    /* スレッドセーフAPI（ミューテックス保護） */
    fsp_err_t hyperram_b_write(const void *p_src, void *p_dest, uint32_t total_length);
    fsp_err_t hyperram_b_read(void *p_dest, const void *p_src, uint32_t total_length);

#ifdef __cplusplus
}
#endif

#endif /* HYPERRAM_INTEG_H */