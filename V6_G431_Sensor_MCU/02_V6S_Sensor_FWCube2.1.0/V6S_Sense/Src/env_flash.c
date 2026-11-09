/**
 * @file  env_flash.c
 * @brief Non-volatile parameter storage using STM32G431KB internal flash.
 *
 * Flash layout (STM32G431KB, 128KB, 2KB pages):
 *   Pages 0-6  : 0x08000000-0x08003800  future IAP bootloader (up to 14KB)
 *   Page  7    : 0x08003800-0x08003FFF  parameter storage (this module)
 *   Pages 8-63 : 0x08004000-0x0801FFFF  application code (linker FLASH, 112KB)
 *
 */

#include "env_flash.h"
#include "main.h"
#include <string.h>

#define ENV_FLASH_PAGE_NUM     7U
#define ENV_FLASH_PAGE_ADDR    (FLASH_BASE + (uint32_t)(ENV_FLASH_PAGE_NUM) * FLASH_PAGE_SIZE)
#define ENV_FLASH_MAGIC        0xA55A1234UL

#define IDX_MAGIC   0U
#define IDX_RESV    1U
#define IDX_PARAMS  2U
#define IDX_CRC     (IDX_PARAMS + ENV_PARM_NUM)

uint32_t env_flash[ENV_PARM_NUM];
uint32_t env_working[ENV_PARM_NUM];

static uint32_t env_xor_chk(const uint32_t *p, uint32_t n)
{
    uint32_t chk = 0U;
    for (uint32_t i = 0U; i < n; i++) {
        chk ^= p[i];
    }
    return chk;
}

void env_flash_default(void)
{
    memset(env_flash, 0, sizeof(env_flash));
    env_flash[PARM_TRACE_FLAG]       = 0U;
    env_flash[PARM_MON_ONOFF]        = 0U;//1U;  //0U
    env_flash[PARM_MON_INTERVAL_SEC] = 1U;
    env_flash[PARM_MON_FLAG]         = 0x07U; //0x0FU; //0x02U; //0x07U
    env_flash[PARM_MON_LINE]         = 1U;
    env_flash[PARM_I2C_POLL_MS]      = 1U;
    env_flash[PARM_UART_DEBUG_ECHO]  = 1U;
}

void env_flash_copy_to_working(void)
{
    memcpy(env_working, env_flash, sizeof(env_working));
}

void env_flash_copy_to_flash_buf(void)
{
    memcpy(env_flash, env_working, sizeof(env_flash));
}

void env_flash_load(void)
{
    const uint32_t *fp = (const uint32_t *)ENV_FLASH_PAGE_ADDR;

    if (fp[IDX_MAGIC] != ENV_FLASH_MAGIC) {
        return;
    }

    uint32_t stored_chk = fp[IDX_CRC];
    uint32_t calc_chk   = env_xor_chk(&fp[IDX_PARAMS], ENV_PARM_NUM);
    if (stored_chk != calc_chk) {
        return;
    }

    memcpy(env_flash, &fp[IDX_PARAMS], ENV_PARM_NUM * sizeof(uint32_t));
}

HAL_StatusTypeDef env_flash_save(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t               page_err = 0U;
    HAL_StatusTypeDef      st;

    st = HAL_FLASH_Unlock();
    if (st != HAL_OK) { return st; }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = FLASH_BANK_1;
    erase.Page      = ENV_FLASH_PAGE_NUM;
    erase.NbPages   = 1U;
    st = HAL_FLASHEx_Erase(&erase, &page_err);
    if (st != HAL_OK) {
        HAL_FLASH_Lock();
        return st;
    }

    uint32_t addr = ENV_FLASH_PAGE_ADDR;

    uint64_t dw = (uint64_t)ENV_FLASH_MAGIC | ((uint64_t)0xFFFFFFFFUL << 32);
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, dw);
    addr += 8U;

    for (uint32_t i = 0U; (i < ENV_PARM_NUM) && (st == HAL_OK); i += 2U) {
        uint32_t lo = env_flash[i];
        uint32_t hi = ((i + 1U) < ENV_PARM_NUM) ? env_flash[i + 1U] : 0xFFFFFFFFUL;
        dw = (uint64_t)lo | ((uint64_t)hi << 32);
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, dw);
        addr += 8U;
    }

    if (st == HAL_OK) {
        uint32_t chk = env_xor_chk(env_flash, ENV_PARM_NUM);
        dw = (uint64_t)chk | ((uint64_t)0xFFFFFFFFUL << 32);
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, dw);
    }

    HAL_FLASH_Lock();
    return st;
}

HAL_StatusTypeDef env_flash_erase(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t               page_err = 0U;
    HAL_StatusTypeDef      st;

    st = HAL_FLASH_Unlock();
    if (st != HAL_OK) { return st; }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = FLASH_BANK_1;
    erase.Page      = ENV_FLASH_PAGE_NUM;
    erase.NbPages   = 1U;
    st = HAL_FLASHEx_Erase(&erase, &page_err);
    HAL_FLASH_Lock();
    return st;
}

void env_flash_init(void)
{
    env_flash_default();
    env_flash_load();
    env_flash_copy_to_working();
}
