/**
 ******************************************************************************
 * @file    ymodem.h
 * @brief   YModem protocol header (MCU-independent)
 ******************************************************************************
 */

#ifndef __YMODEM_H_
#define __YMODEM_H_

/* COM status ---------------------------------------------------------------*/
typedef enum {
    COM_OK      = 0x00,
    COM_ERROR   = 0x01,
    COM_ABORT   = 0x02,
    COM_TIMEOUT = 0x03,
    COM_DATA    = 0x04,
    COM_LIMIT   = 0x05
} COM_StatusTypeDef;

/* Packet defines -----------------------------------------------------------*/
#define PACKET_HEADER_SIZE      ((uint32_t)3)
#define PACKET_DATA_INDEX       ((uint32_t)4)
#define PACKET_START_INDEX      ((uint32_t)1)
#define PACKET_NUMBER_INDEX     ((uint32_t)2)
#define PACKET_CNUMBER_INDEX    ((uint32_t)3)
#define PACKET_TRAILER_SIZE     ((uint32_t)2)
#define PACKET_OVERHEAD_SIZE    (PACKET_HEADER_SIZE + PACKET_TRAILER_SIZE - 1)
#define PACKET_SIZE             ((uint32_t)128)
#define PACKET_1K_SIZE          ((uint32_t)1024)

#define FILE_NAME_LENGTH        ((uint32_t)64)
#define FILE_SIZE_LENGTH        ((uint32_t)16)

/* Protocol bytes -----------------------------------------------------------*/
#define SOH                     ((uint8_t)0x01)
#define STX                     ((uint8_t)0x02)
#define EOT                     ((uint8_t)0x04)
#define ACK                     ((uint8_t)0x06)
#define NAK                     ((uint8_t)0x15)
#define CA                      ((uint32_t)0x18)
#define CRC16                   ((uint8_t)0x43)
#define NEGATIVE_BYTE           ((uint8_t)0xFF)
#define ABORT1                  ((uint8_t)0x41)  /* 'A' */
#define ABORT2                  ((uint8_t)0x61)  /* 'a' */

#define NAK_TIMEOUT             ((uint32_t)0x100000)
#define DOWNLOAD_TIMEOUT        ((uint32_t)1000)
#define MAX_ERRORS              ((uint32_t)5)

/* Debug/compat toggles ----------------------------------------------------*/
#define YMODEM_DROP_DUP_SOH     0u
#define YMODEM_DRAIN_ON_BAD_HDR 0u
#define YMODEM_HEADER_WAIT_MS   ((uint32_t)3000)

/* Exported functions -------------------------------------------------------*/
COM_StatusTypeDef Ymodem_Receive(uint32_t *p_size);
COM_StatusTypeDef Ymodem_Transmit(uint8_t *p_buf, const uint8_t *p_file_name, uint32_t file_size);

#endif /* __YMODEM_H_ */
