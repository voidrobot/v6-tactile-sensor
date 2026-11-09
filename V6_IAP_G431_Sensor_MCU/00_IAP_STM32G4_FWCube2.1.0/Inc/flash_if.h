/**
 ******************************************************************************
 * @file    flash_if.h
 * @brief   Flash interface for IAP - STM32G431KB
 *
 *  Flash layout (128 KB total, page = 2 KB):
 *    Pages  0~ 7  (0x08000000 ~ 0x08003FFF, 16 KB)  IAP bootloader
 *    Pages  8~63  (0x08004000 ~ 0x0801FFFF, 112 KB) User application
 ******************************************************************************
 */

#ifndef __FLASH_IF_H
#define __FLASH_IF_H

#include "stm32g4xx_hal.h"

#define ADDR_FLASH_PAGE_0       ((uint32_t)0x08000000)
#define ADDR_FLASH_PAGE_8       ((uint32_t)0x08004000)
#define ADDR_FLASH_PAGE_64      ((uint32_t)0x08020000)

#define USER_START_ADDRESS      ADDR_FLASH_PAGE_8
#define USER_END_ADDRESS        ADDR_FLASH_PAGE_64
#define USER_FLASH_SIZE         ((uint32_t)(USER_END_ADDRESS - USER_START_ADDRESS))
#define USER_FLASH_FIRST_PAGE   8u
#define USER_FLASH_NB_PAGES     56u

enum {
    FLASHIF_OK = 0,
    FLASHIF_ERASEKO,
    FLASHIF_WRITINGCTRL_ERROR,
    FLASHIF_WRITING_ERROR,
    FLASHIF_PROTECTION_ERRROR
};

enum {
    FLASHIF_PROTECTION_NONE = 0,
    FLASHIF_PROTECTION_PCROPENABLED = 0x1,
    FLASHIF_PROTECTION_WRPENABLED = 0x2,
    FLASHIF_PROTECTION_RDPENABLED = 0x4,
};

enum {
    FLASHIF_WRP_ENABLE,
    FLASHIF_WRP_DISABLE
};

void     FLASH_If_Init(void);
uint32_t FLASH_If_Erase(uint32_t StartAddress, uint32_t image_size_bytes);
uint32_t FLASH_If_GetWriteProtectionStatus(void);
uint32_t FLASH_If_Write(uint32_t destination, uint32_t *p_source, uint32_t length);
uint32_t FLASH_If_WriteProtectionConfig(uint32_t modifier);

#endif /* __FLASH_IF_H */
