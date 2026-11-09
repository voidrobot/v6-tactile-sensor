/**
 ******************************************************************************
 * @file    menu.h
 * @brief   IAP main menu header - STM32G431
 ******************************************************************************
 */

#ifndef __MENU_H
#define __MENU_H

#include "flash_if.h"
#include "ymodem.h"

/* Imported variables --------------------------------------------------------*/
extern uint8_t aFileName[FILE_NAME_LENGTH];

/* Private variables ---------------------------------------------------------*/
typedef void (*pFunction)(void);

/* Exported functions -------------------------------------------------------*/
void              Main_Menu(void);
COM_StatusTypeDef SerialDownload(void);

#endif /* __MENU_H */
