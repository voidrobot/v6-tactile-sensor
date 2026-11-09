/**
 ******************************************************************************
 * @file    flash_if.c
 * @brief   Flash interface for IAP - STM32G431KB
 ******************************************************************************
 */

#include "flash_if.h"
#include "main.h"

static void flash_clear_flags(void)
{
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_ALL_ERRORS);
}

void FLASH_If_Init(void)
{
    HAL_FLASH_Unlock();
    flash_clear_flags();
    HAL_FLASH_Lock();
}

uint32_t FLASH_If_Erase(uint32_t StartAddress, uint32_t image_size_bytes)
{
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError = 0;
    uint32_t pages;

    if (StartAddress != USER_START_ADDRESS) {
        return FLASHIF_ERASEKO;
    }

    if ((image_size_bytes == 0u) || (image_size_bytes > USER_FLASH_SIZE)) {
        return FLASHIF_ERASEKO;
    }

    pages = (image_size_bytes + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE;
    if (pages == 0u) {
        pages = 1u;
    }
    if (pages > USER_FLASH_NB_PAGES) {
        pages = USER_FLASH_NB_PAGES;
    }

    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.Banks     = FLASH_BANK_1;
    eraseInit.Page      = USER_FLASH_FIRST_PAGE;
    eraseInit.NbPages   = pages;

    HAL_FLASH_Unlock();
    flash_clear_flags();

    if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK) {
        HAL_FLASH_Lock();
        return FLASHIF_ERASEKO;
    }

    HAL_FLASH_Lock();
    return FLASHIF_OK;
}

uint32_t FLASH_If_Write(uint32_t destination, uint32_t *p_source, uint32_t length)
{
    uint32_t dest = destination;
    uint8_t *src = (uint8_t *)p_source;
    uint32_t bytes = length * 4u;

    if ((dest < USER_START_ADDRESS) || ((dest + bytes) > USER_END_ADDRESS) || ((dest & 0x7u) != 0u)) {
        return FLASHIF_WRITING_ERROR;
    }

    HAL_FLASH_Unlock();
    flash_clear_flags();

    for (uint32_t offset = 0; offset < bytes; offset += 8u) {
        uint64_t dw = 0xFFFFFFFFFFFFFFFFULL;
        uint32_t remain = bytes - offset;
        uint32_t copy_len = (remain >= 8u) ? 8u : remain;

        for (uint32_t i = 0; i < copy_len; i++) {
            dw &= ~((uint64_t)0xFFu << (8u * i));
            dw |= ((uint64_t)src[offset + i] << (8u * i));
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, dest, dw) != HAL_OK) {
            HAL_FLASH_Lock();
            return FLASHIF_WRITING_ERROR;
        }

        if (*(uint64_t *)dest != dw) {
            HAL_FLASH_Lock();
            return FLASHIF_WRITING_ERROR;
        }

        dest += 8u;
    }

    HAL_FLASH_Lock();
    return FLASHIF_OK;
}

uint32_t FLASH_If_GetWriteProtectionStatus(void)
{
    return FLASHIF_PROTECTION_NONE;
}

uint32_t FLASH_If_WriteProtectionConfig(uint32_t modifier)
{
    (void)modifier;
    return FLASHIF_OK;
}
