#include "hal_data.h"
#include "xprintf_helper.h"

typedef struct
{
    uint8_t reg_high; // 上位アドレス
    uint8_t reg_low;  // 下位アドレス
    uint8_t val;      // 書き込みたい値
} cam_reg_value_t;

// init
void sccb_init(void);
// I2C write
int32_t reg_write(uint32_t addr,
                  uint8_t *buf,
                  const uint8_t nbytes);
// I2C reads
int32_t reg_read(uint32_t addr,
                 uint8_t *buf,
                 const uint8_t nbytes);

// CALLBACK
extern void g_i2c_callback(i2c_master_callback_args_t *p_args);
