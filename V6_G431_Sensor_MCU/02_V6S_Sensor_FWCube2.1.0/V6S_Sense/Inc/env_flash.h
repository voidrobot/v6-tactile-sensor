#ifndef ENV_FLASH_H
#define ENV_FLASH_H

#include <stdint.h>
#include "parms.h"
#include "stm32g4xx_hal.h"

extern uint32_t env_flash[ENV_PARM_NUM];
extern uint32_t env_working[ENV_PARM_NUM];

void              env_flash_init             (void);
void              env_flash_default          (void);
void              env_flash_load             (void);
HAL_StatusTypeDef env_flash_save             (void);
HAL_StatusTypeDef env_flash_erase            (void);
void              env_flash_copy_to_working  (void);
void              env_flash_copy_to_flash_buf(void);

#endif
