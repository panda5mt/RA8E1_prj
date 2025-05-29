/* generated configuration header file - do not edit */
#ifndef CC_H_
#define CC_H_
#include "bsp_api.h"

#define LWIP_PLATFORM_ASSERT(x)    do {FSP_LOG_PRINT(x); BSP_CFG_HANDLE_UNRECOVERABLE_ERROR(0);} while (0)
#define LWIP_RAND()                ((u32_t) rand())

#if defined(__ICCARM__)
 #define PACK_STRUCT_BEGIN    __packed
#endif
#endif /* CC_H_ */
