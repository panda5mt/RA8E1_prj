#include "hal_data.h"
#include "xprintf_helper.h"

#define DEV_OV5642 (1)
#define DEV_OV2640 (2)

void sccb_init(uint8_t device_is); // e.g.DEV_OV5642

int32_t reg_write(uint8_t addr,
                  uint8_t *buf,
                  const uint8_t nbytes);

int32_t reg_read(uint8_t addr,
                 uint8_t *buf,
                 const uint8_t nbytes);