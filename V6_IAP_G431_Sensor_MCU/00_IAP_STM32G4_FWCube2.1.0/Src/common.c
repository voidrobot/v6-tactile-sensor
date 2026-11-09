/**
 ******************************************************************************
 * @file    common.c
 * @brief   Common utility functions for IAP (ported to STM32G431)
 ******************************************************************************
 */

#include "common.h"
#include "main.h"

void Int2Str(uint8_t *p_str, uint32_t intnum)
{
    uint32_t i, divider = 1000000000, pos = 0, status = 0;

    for (i = 0; i < 10; i++) {
        p_str[pos++] = (uint8_t)((intnum / divider) + 48u);
        intnum = intnum % divider;
        divider /= 10;
        if ((p_str[pos - 1] == '0') && (status == 0)) {
            pos = 0;
        } else {
            status++;
        }
    }
}

uint32_t Str2Int(uint8_t *inputstr, uint32_t *intnum)
{
    uint32_t i = 0, res = 0, val = 0;

    if ((inputstr[0] == '0') && ((inputstr[1] == 'x') || (inputstr[1] == 'X'))) {
        i = 2;
        while ((i < 11) && (inputstr[i] != '\0')) {
            if (ISVALIDHEX(inputstr[i])) {
                val = (val << 4) + CONVERTHEX(inputstr[i]);
            } else {
                res = 0;
                break;
            }
            i++;
        }
        if (inputstr[i] == '\0') {
            *intnum = val;
            res = 1;
        }
    } else {
        while ((i < 11) && (res != 1)) {
            if (inputstr[i] == '\0') {
                *intnum = val;
                res = 1;
            } else if (((inputstr[i] == 'k') || (inputstr[i] == 'K')) && (i > 0)) {
                val = val << 10;
                *intnum = val;
                res = 1;
            } else if (((inputstr[i] == 'm') || (inputstr[i] == 'M')) && (i > 0)) {
                val = val << 20;
                *intnum = val;
                res = 1;
            } else if (ISVALIDDEC(inputstr[i])) {
                val = val * 10u + CONVERTDEC(inputstr[i]);
            } else {
                res = 0;
                break;
            }
            i++;
        }
    }
    return res;
}

void Serial_PutString(uint8_t *p_string)
{
    uint16_t length = 0;
    while (p_string[length] != '\0') { length++; }
    HAL_UART_Transmit(&UartHandle, p_string, length, TX_TIMEOUT);
}

HAL_StatusTypeDef Serial_PutByte(uint8_t param)
{
    if (UartHandle.gState == HAL_UART_STATE_TIMEOUT) {
        UartHandle.gState = HAL_UART_STATE_READY;
    }
    return HAL_UART_Transmit(&UartHandle, &param, 1, TX_TIMEOUT);
}
