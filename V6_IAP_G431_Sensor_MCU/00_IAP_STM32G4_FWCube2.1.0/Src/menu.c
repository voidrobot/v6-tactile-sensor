/**
 ******************************************************************************
 * @file    menu.c
 * @brief   Main IAP menu - ported to STM32G431KB
 ******************************************************************************
 */

#include "main.h"
#include "common.h"
#include "flash_if.h"
#include "menu.h"
#include "ymodem.h"

/* Diagnostic counters from ymodem.c */
extern volatile uint32_t ym_recv_calls;
extern volatile uint32_t ym_recv_ok;
extern volatile uint32_t ym_recv_timeout;
extern volatile uint32_t ym_recv_error;
extern volatile uint32_t ym_recv_busy;
extern volatile uint32_t ym_crc_fail;
extern volatile uint32_t ym_seq_fail;
extern volatile uint32_t ym_pkt_ok;
extern volatile uint32_t ym_erase_ret;
extern volatile uint8_t  ym_last_char1;
extern volatile uint32_t ym_last_uart_err;
extern volatile uint32_t ym_hdr_ok;
extern volatile uint32_t ym_hdr_fail;
extern volatile uint32_t ym_last_pkt_len;
extern volatile uint32_t ym_last_seq;
extern volatile uint32_t ym_last_cseq;
extern volatile uint32_t ym_last_crc_rx;
extern volatile uint32_t ym_last_crc_cal;
extern volatile uint32_t ym_last_filesz;
extern volatile uint32_t ym_dbg_stage;
extern volatile uint32_t ym_dbg_arg;
extern volatile uint32_t ym_dbg_t0;
extern volatile uint32_t ym_dbg_dt;

/* Private define ------------------------------------------------------------*/
#define BOOT_TIMEOUT_2S  2000u
#define BOOT_TIMEOUT_3S  3000u

#define ESC_IN_SERIES    3u

/* Private variables ---------------------------------------------------------*/
pFunction JumpToApplication;
uint32_t  JumpAddress;
uint32_t  FlashProtection = 0;
uint8_t   aFileName[FILE_NAME_LENGTH];

extern uint8_t  IAP_mode;
extern uint32_t cnt_ms;
extern uint32_t tick_cnt;

/* Private function prototypes -----------------------------------------------*/
COM_StatusTypeDef SerialDownload(void);
static void SerialUpload(void) __attribute__((unused));

/* ---------------------------------------------------------------------------
 * SerialDownload
 * Receive a binary file over Ymodem and write it to user flash.
 * --------------------------------------------------------------------------- */
COM_StatusTypeDef SerialDownload(void)
{
    uint8_t number[11] = {0};
    uint32_t size = 0;
    COM_StatusTypeDef result;

    Serial_PutString((uint8_t *)"Waiting for the file to be sent ... (press 'a' to abort)\n\r");
    result = Ymodem_Receive(&size);

    /* ---- Diagnostic dump ---- */
    Serial_PutString((uint8_t *)"\r\n--- YM DIAG ---\r\n");
    Int2Str(number, ym_recv_calls);
    Serial_PutString((uint8_t *)"recv_calls="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_recv_ok);
    Serial_PutString((uint8_t *)"recv_ok="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_recv_timeout);
    Serial_PutString((uint8_t *)"recv_tmo="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_recv_error);
    Serial_PutString((uint8_t *)"recv_err="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_recv_busy);
    Serial_PutString((uint8_t *)"recv_busy="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_crc_fail);
    Serial_PutString((uint8_t *)"crc_fail="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_seq_fail);
    Serial_PutString((uint8_t *)"seq_fail="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_pkt_ok);
    Serial_PutString((uint8_t *)"pkt_ok="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_erase_ret);
    Serial_PutString((uint8_t *)"erase_ret="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, (uint32_t)ym_last_char1);
    Serial_PutString((uint8_t *)"last_ch1=0x"); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_last_uart_err);
    Serial_PutString((uint8_t *)"uart_err=0x"); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_hdr_ok);
    Serial_PutString((uint8_t *)"hdr_ok="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_hdr_fail);
    Serial_PutString((uint8_t *)"hdr_fail="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_last_pkt_len);
    Serial_PutString((uint8_t *)"last_pkt_len="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_last_seq);
    Serial_PutString((uint8_t *)"last_seq="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_last_cseq);
    Serial_PutString((uint8_t *)"last_cseq="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_last_crc_rx);
    Serial_PutString((uint8_t *)"last_crc_rx="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_last_crc_cal);
    Serial_PutString((uint8_t *)"last_crc_cal="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Int2Str(number, ym_last_filesz);
    Serial_PutString((uint8_t *)"last_filesz="); Serial_PutString(number); Serial_PutString((uint8_t *)"\r\n");
    Serial_PutString((uint8_t *)"--- END DIAG ---\r\n");

    if (result == COM_OK) {
        HAL_Delay(500);
        Serial_PutString((uint8_t *)"\n\n\r Programming Completed Successfully!\n\r--------------------------------\r\n Name: ");
        Serial_PutString(aFileName);
        Int2Str(number, size);
        Serial_PutString((uint8_t *)"\n\r Size: ");
        Serial_PutString(number);
        Serial_PutString((uint8_t *)" Bytes\r\n");
        Serial_PutString((uint8_t *)"-------------------\n");
    } else if (result == COM_LIMIT) {
        Serial_PutString((uint8_t *)"\n\n\rThe image size is higher than the allowed space memory!\n\r");
    } else if (result == COM_DATA) {
        Serial_PutString((uint8_t *)"\n\n\rVerification failed!\n\r");
    } else if (result == COM_ABORT) {
        Serial_PutString((uint8_t *)"\r\n\nAborted by user.\n\r");
    } else {
        Serial_PutString((uint8_t *)"\n\rFailed to receive the file!\n\r");
    }

    IAP_mode = 0;
    return result;
}

/* ---------------------------------------------------------------------------
 * SerialUpload
 * Transmit the user flash region over Ymodem.
 * --------------------------------------------------------------------------- */
static void SerialUpload(void)
{
    uint8_t status = 0;

    Serial_PutString((uint8_t *)"\n\n\rSelect Receive File\n\r");

    HAL_UART_Receive(&UartHandle, &status, 1, RX_TIMEOUT);
    if (status == CRC16) {
        status = (uint8_t)Ymodem_Transmit((uint8_t *)USER_START_ADDRESS,
                                          (const uint8_t *)"UploadedFlashImage.bin",
                                          USER_FLASH_SIZE);
        if (status != 0) {
            Serial_PutString((uint8_t *)"\n\rError Occurred while Transmitting File\n\r");
        } else {
            Serial_PutString((uint8_t *)"\n\rFile uploaded successfully \n\r");
        }
    }
}

/* ---------------------------------------------------------------------------
 * Main_Menu
 * 2-second boot countdown. ESC×3 = ymodem download, 'r'×3 = reset.
 * Returns on timeout so caller can jump to user app.
 * --------------------------------------------------------------------------- */
void Main_Menu(void)
{
    uint8_t  key       = 0;
    uint32_t bytes_read = 0;

    Serial_PutString((uint8_t *)"\r\n======================================================================");
    Serial_PutString((uint8_t *)"\r\n=             IAP(In-Application Programming) By Rick.               =");
    Serial_PutString((uint8_t *)"\r\n=                                                                    =");
    Serial_PutString((uint8_t *)"\r\n=             - for STM32G431,   Version 1.0                         =");
    Serial_PutString((uint8_t *)"\r\n======================================================================");
    Serial_PutString((uint8_t *)"\r\n\r\n");

    FlashProtection = FLASH_If_GetWriteProtectionStatus();

    if (ym_dbg_stage != 0u) {
        uint8_t number[11] = {0};
        Serial_PutString((uint8_t *)"\r\n[YM-RAM] stage=");
        Int2Str(number, ym_dbg_stage); Serial_PutString(number);
        Serial_PutString((uint8_t *)" arg=");
        Int2Str(number, ym_dbg_arg); Serial_PutString(number);
        Serial_PutString((uint8_t *)" dt_ms=");
        Int2Str(number, ym_dbg_dt); Serial_PutString(number);
        Serial_PutString((uint8_t *)"\r\n");
    }

    cnt_ms    = BOOT_TIMEOUT_2S;
    bytes_read = 0;

    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    uint32_t last_dot_tick = tick_cnt;

    while (1) {
        if ((tick_cnt - last_dot_tick) >= 250u) {
            Serial_PutString((uint8_t *)".");
            last_dot_tick = tick_cnt;
        }

        /* Try to receive one byte with a 2 ms timeout */
        if (HAL_UART_Receive(&UartHandle, &key, 1, 2u) == HAL_OK) {
            if (key == 0x1Bu) {    /* ESC */
                bytes_read++;
                if (bytes_read == ESC_IN_SERIES) {
                    Serial_PutString((uint8_t *)("\r\n"));

                    if (SerialDownload() == COM_OK) {
                        Serial_PutString((uint8_t *)"\r\nDownloaded program start ...\r\n\n");
                        HAL_Delay(1000);

                        HAL_RCC_DeInit();
                        HAL_DeInit();
                        SysTick->CTRL = 0;
                        SysTick->LOAD = 0;
                        SysTick->VAL  = 0;

                        JumpAddress = *(__IO uint32_t *)(USER_START_ADDRESS + 4u);
                        JumpToApplication = (pFunction)JumpAddress;
                        __set_MSP(*(__IO uint32_t *)USER_START_ADDRESS);
                        JumpToApplication();
                    } else {
                        bytes_read = 0;
                        cnt_ms = BOOT_TIMEOUT_3S;
                        Serial_PutString((uint8_t *)("\r\n"));
                    }
                }
            } else if ((key == 'r') || (key == 'R')) {
                bytes_read++;
                if (bytes_read == 3u) {
                    Serial_PutString((uint8_t *)("\r\nRestart...\r\n"));
                    HAL_Delay(100);
                    __NVIC_SystemReset();
                }
            } else {
                bytes_read = 0;
            }
        }

        /* Boot timeout expired → return to caller to jump to app */
        if ((cnt_ms == 0u) || (cnt_ms > BOOT_TIMEOUT_2S)) {
            return;
        }
    }
}
