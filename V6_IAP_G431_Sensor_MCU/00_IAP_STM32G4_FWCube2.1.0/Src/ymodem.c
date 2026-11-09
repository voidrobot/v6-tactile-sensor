/**
 ******************************************************************************
 * @file    ymodem.c
 * @brief   Ymodem protocol - ported to STM32G431KB
 *          (include paths updated; flash write uses HALFWORD via flash_if.c)
 ******************************************************************************
 */

#include "flash_if.h"
#include "common.h"
#include "ymodem.h"
#include "menu.h"
#include "string.h"
#include "main.h"

/* Private define ------------------------------------------------------------*/
#define CRC16_F   /* use CRC16 integrity */

/* Private variables ---------------------------------------------------------*/
/* NOTE: keep 32-bit aligned */
uint8_t aPacketData[PACKET_1K_SIZE + PACKET_DATA_INDEX + PACKET_TRAILER_SIZE];

extern uint8_t IAP_mode;

/* Diagnostic counters — printed after transfer for debugging */
volatile uint32_t ym_recv_calls   = 0;  /* ReceivePacket call count */
volatile uint32_t ym_recv_ok      = 0;  /* HAL_OK count */
volatile uint32_t ym_recv_timeout = 0;  /* HAL_TIMEOUT count */
volatile uint32_t ym_recv_error   = 0;  /* HAL_ERROR count */
volatile uint32_t ym_recv_busy    = 0;  /* HAL_BUSY count */
volatile uint32_t ym_crc_fail     = 0;  /* CRC mismatch */
volatile uint32_t ym_seq_fail     = 0;  /* seq mismatch */
volatile uint32_t ym_pkt_ok       = 0;  /* good packets written */
volatile uint32_t ym_erase_ret    = 0;  /* FLASH_If_Erase return */
volatile uint8_t  ym_last_char1   = 0;  /* last first-byte received */
volatile uint32_t ym_last_uart_err = 0; /* last HAL ErrorCode */
volatile uint32_t ym_hdr_ok       = 0;  /* packet 0 parsed OK */
volatile uint32_t ym_hdr_fail     = 0;  /* packet 0 parse fail */
volatile uint32_t ym_last_pkt_len = 0;  /* last packet length */
volatile uint32_t ym_last_seq     = 0;  /* last packet seq */
volatile uint32_t ym_last_cseq    = 0;  /* last packet cseq */
volatile uint32_t ym_last_crc_rx  = 0;  /* last CRC from sender */
volatile uint32_t ym_last_crc_cal = 0;  /* last calculated CRC */
volatile uint32_t ym_last_filesz  = 0;  /* last parsed filesize */
volatile uint32_t ym_dbg_stage    = 0;  /* RAM snapshot: current Ymodem stage */
volatile uint32_t ym_dbg_arg      = 0;  /* RAM snapshot: stage argument */
volatile uint32_t ym_dbg_t0       = 0;  /* RAM snapshot: stage start tick */
volatile uint32_t ym_dbg_dt       = 0;  /* RAM snapshot: measured duration */

/* Private function prototypes -----------------------------------------------*/
static void PrepareIntialPacket(uint8_t *p_data, const uint8_t *p_file_name, uint32_t length);
static void PreparePacket(uint8_t *p_source, uint8_t *p_packet, uint8_t pkt_nr, uint32_t size_blk);
static HAL_StatusTypeDef ReceivePacket(uint8_t *p_data, uint32_t *p_length, uint32_t timeout);
uint16_t UpdateCRC16(uint16_t crc_in, uint8_t byte);
uint16_t Cal_CRC16(const uint8_t *p_data, uint32_t size);
uint8_t  CalcChecksum(const uint8_t *p_data, uint32_t size);

/* ---------------------------------------------------------------------------
 * ReceivePacket
 * Receive one Ymodem packet.
 * Returns HAL_OK, HAL_BUSY (abort), or HAL_ERROR.
 * p_length: 0=EOT, 2=abort-by-sender, 128/1024=data
 * --------------------------------------------------------------------------- */
static HAL_StatusTypeDef ReceivePacket(uint8_t *p_data, uint32_t *p_length, uint32_t timeout)
{
    uint32_t crc;
    uint32_t packet_size = 0;
    HAL_StatusTypeDef status;
    uint8_t char1;

    ym_recv_calls++;

    /* Clear any pending UART errors (ORE/FE/NE) before receiving */
    if (__HAL_UART_GET_FLAG(&UartHandle, UART_FLAG_ORE)) {
        __HAL_UART_CLEAR_FLAG(&UartHandle, UART_CLEAR_OREF);
    }
    if (__HAL_UART_GET_FLAG(&UartHandle, UART_FLAG_FE)) {
        __HAL_UART_CLEAR_FLAG(&UartHandle, UART_CLEAR_FEF);
    }
    if (__HAL_UART_GET_FLAG(&UartHandle, UART_FLAG_NE)) {
        __HAL_UART_CLEAR_FLAG(&UartHandle, UART_CLEAR_NEF);
    }

    *p_length = 0;
    status = HAL_UART_Receive(&UartHandle, &char1, 1, timeout);

    /* Duplicate SOH workaround is kept as an optional compatibility toggle.
     * Default is OFF because some senders can be misdetected and packet 0 gets
     * shifted by one byte, which freezes progress at header stage. */
#if YMODEM_DROP_DUP_SOH
    if ((status == HAL_OK) && (char1 == SOH) && (aPacketData[0] == SOH)) {
        status = HAL_UART_Receive(&UartHandle, &char1, 1, timeout);
    }
#endif

    ym_last_char1 = char1;
    ym_last_uart_err = UartHandle.ErrorCode;

    if (status == HAL_OK) {
        switch (char1) {
        case SOH:
            packet_size = PACKET_SIZE;
            break;
        case STX:
            packet_size = PACKET_1K_SIZE;
            break;
        case EOT:
            break;
        case CA:
            if ((HAL_UART_Receive(&UartHandle, &char1, 1, timeout) == HAL_OK) && (char1 == CA)) {
                packet_size = 2;
            } else {
                status = HAL_ERROR;
            }
            break;
        case ABORT1:
        case ABORT2:
            status = HAL_BUSY;
            break;
        default:
            /* Some senders/terminal bridges can leak XON/XOFF or trailing
             * control bytes near end-of-transfer. Ignore them as soft noise
             * instead of poisoning the whole session. */
            if ((char1 == 0x11u) || (char1 == 0x13u)) {
                status = HAL_TIMEOUT;
                packet_size = 0u;
                break;
            }
#if YMODEM_DRAIN_ON_BAD_HDR
            {
                uint8_t drain;
                __HAL_UART_CLEAR_FLAG(&UartHandle, UART_CLEAR_OREF);
                __HAL_UART_CLEAR_FLAG(&UartHandle, UART_CLEAR_FEF);
                __HAL_UART_CLEAR_FLAG(&UartHandle, UART_CLEAR_NEF);
                while (HAL_UART_Receive(&UartHandle, &drain, 1, 50U) == HAL_OK) {}
            }
#endif
            status = HAL_ERROR;
            break;
        }
        *p_data = char1;

        if (packet_size >= PACKET_SIZE) {
            IAP_mode = 1;
            status = HAL_UART_Receive(&UartHandle, &p_data[PACKET_NUMBER_INDEX],
                                      packet_size + PACKET_OVERHEAD_SIZE, timeout);
            ym_last_uart_err = UartHandle.ErrorCode;

            if (status == HAL_OK) {
                ym_last_seq = p_data[PACKET_NUMBER_INDEX];
                ym_last_cseq = p_data[PACKET_CNUMBER_INDEX];
                if (p_data[PACKET_NUMBER_INDEX] != ((p_data[PACKET_CNUMBER_INDEX]) ^ NEGATIVE_BYTE)) {
                    ym_seq_fail++;
                    packet_size = 0;
                    status = HAL_ERROR;
                } else {
                    crc  = (uint32_t)p_data[packet_size + PACKET_DATA_INDEX] << 8u;
                    crc += p_data[packet_size + PACKET_DATA_INDEX + 1u];
                    ym_last_crc_rx = crc;
                    ym_last_crc_cal = Cal_CRC16(&p_data[PACKET_DATA_INDEX], packet_size);
                    if (ym_last_crc_cal != crc) {
                        ym_crc_fail++;
                        packet_size = 0;
                        status = HAL_ERROR;
                    }
                }
            } else {
                packet_size = 0;
            }
        }
    }
    *p_length = packet_size;
    ym_last_pkt_len = packet_size;

    /* Update diagnostic counters */
    if (status == HAL_OK)       ym_recv_ok++;
    else if (status == HAL_TIMEOUT) ym_recv_timeout++;
    else if (status == HAL_ERROR)   ym_recv_error++;
    else if (status == HAL_BUSY)    ym_recv_busy++;

    return status;
}

/* ---------------------------------------------------------------------------
 * PrepareIntialPacket — build Ymodem header (packet 0)
 * --------------------------------------------------------------------------- */
static void PrepareIntialPacket(uint8_t *p_data, const uint8_t *p_file_name, uint32_t length)
{
    uint32_t i, j = 0;
    uint8_t astring[10];

    p_data[PACKET_START_INDEX]   = SOH;
    p_data[PACKET_NUMBER_INDEX]  = 0x00u;
    p_data[PACKET_CNUMBER_INDEX] = 0xFFu;

    for (i = 0; (p_file_name[i] != '\0') && (i < FILE_NAME_LENGTH); i++) {
        p_data[i + PACKET_DATA_INDEX] = p_file_name[i];
    }
    p_data[i + PACKET_DATA_INDEX] = 0x00u;

    Int2Str(astring, length);
    i = i + PACKET_DATA_INDEX + 1u;
    while (astring[j] != '\0') {
        p_data[i++] = astring[j++];
    }

    for (j = i; j < PACKET_SIZE + PACKET_DATA_INDEX; j++) {
        p_data[j] = 0u;
    }
}

/* ---------------------------------------------------------------------------
 * PreparePacket — build a data packet
 * --------------------------------------------------------------------------- */
static void PreparePacket(uint8_t *p_source, uint8_t *p_packet, uint8_t pkt_nr, uint32_t size_blk)
{
    uint8_t  *p_record;
    uint32_t  i, size, packet_size;

    packet_size = (size_blk >= PACKET_1K_SIZE) ? PACKET_1K_SIZE : PACKET_SIZE;
    size        = (size_blk < packet_size)      ? size_blk       : packet_size;

    p_packet[PACKET_START_INDEX]   = (packet_size == PACKET_1K_SIZE) ? STX : SOH;
    p_packet[PACKET_NUMBER_INDEX]  = pkt_nr;
    p_packet[PACKET_CNUMBER_INDEX] = (uint8_t)(~pkt_nr);

    p_record = p_source;
    for (i = PACKET_DATA_INDEX; i < size + PACKET_DATA_INDEX; i++) {
        p_packet[i] = *p_record++;
    }
    if (size <= packet_size) {
        for (i = size + PACKET_DATA_INDEX; i < packet_size + PACKET_DATA_INDEX; i++) {
            p_packet[i] = 0x1Au; /* EOF padding */
        }
    }
}

/* ---------------------------------------------------------------------------
 * UpdateCRC16 / Cal_CRC16 / CalcChecksum
 * --------------------------------------------------------------------------- */
uint16_t UpdateCRC16(uint16_t crc_in, uint8_t byte)
{
    uint32_t crc = crc_in;
    uint32_t in  = byte | 0x100u;
    do {
        crc <<= 1;
        in  <<= 1;
        if (in  & 0x100u) ++crc;
        if (crc & 0x10000u) crc ^= 0x1021u;
    } while (!(in & 0x10000u));
    return (uint16_t)(crc & 0xFFFFu);
}

uint16_t Cal_CRC16(const uint8_t *p_data, uint32_t size)
{
    /* CRC-CCITT Ymodem variant: augment with two zero bytes after data.
     * TeraTerm (and ST reference) use this same augmented form.
     * Do NOT remove the two UpdateCRC16(crc, 0) calls. */
    uint32_t crc = 0;
    const uint8_t *dataEnd = p_data + size;
    while (p_data < dataEnd) { crc = UpdateCRC16((uint16_t)crc, *p_data++); }
    crc = UpdateCRC16((uint16_t)crc, 0);
    crc = UpdateCRC16((uint16_t)crc, 0);
    return (uint16_t)(crc & 0xFFFFu);
}

uint8_t CalcChecksum(const uint8_t *p_data, uint32_t size)
{
    uint32_t sum = 0;
    const uint8_t *p_end = p_data + size;
    while (p_data < p_end) { sum += *p_data++; }
    return (uint8_t)(sum & 0xFFu);
}

/* ===========================================================================
 * Ymodem_Receive
 * Receive a file via Ymodem-CRC16 and program it into user flash.
 * =========================================================================== */
COM_StatusTypeDef Ymodem_Receive(uint32_t *p_size)
{
    uint32_t i, packet_length, session_done = 0, file_done, errors = 0, session_begin = 0;
    uint32_t flashdestination, ramsource, filesize;
    uint8_t *file_ptr;

    /* Reset diagnostic counters */
    ym_recv_calls = ym_recv_ok = ym_recv_timeout = ym_recv_error = ym_recv_busy = 0;
    ym_crc_fail = ym_seq_fail = ym_pkt_ok = ym_erase_ret = 0;
    ym_last_char1 = 0; ym_last_uart_err = 0;
    ym_hdr_ok = ym_hdr_fail = 0;
    ym_last_pkt_len = ym_last_seq = ym_last_cseq = 0;
    ym_last_crc_rx = ym_last_crc_cal = ym_last_filesz = 0;
    ym_dbg_stage = 1; ym_dbg_arg = 0; ym_dbg_t0 = HAL_GetTick(); ym_dbg_dt = 0;
    uint8_t  file_size[FILE_SIZE_LENGTH], tmp;
    uint32_t packets_received;
    COM_StatusTypeDef result = COM_OK;

    flashdestination = USER_START_ADDRESS;

    /* Ymodem handshake: receiver sends 'C' first to select CRC-16 mode.
     * Send immediately so the PC sender does not have to wait for the
     * first ReceivePacket timeout (1 s) before seeing the handshake byte. */
    Serial_PutByte(CRC16);

    while ((session_done == 0u) && (result == COM_OK)) {
        packets_received = 0;
        file_done = 0;
        while ((file_done == 0u) && (result == COM_OK)) {
            switch (ReceivePacket(aPacketData, &packet_length,
                                  (session_begin == 0u) ? YMODEM_HEADER_WAIT_MS : DOWNLOAD_TIMEOUT)) {
            case HAL_OK:
                errors = 0;
                switch (packet_length) {
                case 2:
                    Serial_PutByte(ACK);
                    result = COM_ABORT;
                    break;
                case 0:
                    /* EOT. Some senders are picky about the exact end-of-session
                     * pacing, so acknowledge and allow the empty closing packet
                     * or immediate session end to follow naturally. */
                    Serial_PutByte(ACK);
                    file_done = 1;
                    ym_dbg_stage = 10; ym_dbg_arg = packets_received;
                    break;
                default:
                    if (aPacketData[PACKET_NUMBER_INDEX] != (uint8_t)(packets_received & 0xFFu)) {
                        Serial_PutByte(NAK);
                    } else {
                        if (packets_received == 0u) {
                            /* Header packet */
                            if (aPacketData[PACKET_DATA_INDEX] != 0u) {
                                i = 0;
                                file_ptr = aPacketData + PACKET_DATA_INDEX;
                                while ((*file_ptr != '\0') && (i < FILE_NAME_LENGTH)) {
                                    aFileName[i++] = *file_ptr++;
                                }
                                aFileName[i++] = '\0';
                                i = 0;
                                file_ptr++;
                                while ((*file_ptr != ' ') && (i < FILE_SIZE_LENGTH)) {
                                    file_size[i++] = *file_ptr++;
                                }
                                file_size[i++] = '\0';
                                ym_dbg_stage = 2; ym_dbg_arg = 0;
                                if (Str2Int(file_size, &filesize) == 0u) {
                                    ym_hdr_fail++;
                                    tmp = CA;
                                    HAL_UART_Transmit(&UartHandle, &tmp, 1, NAK_TIMEOUT);
                                    HAL_UART_Transmit(&UartHandle, &tmp, 1, NAK_TIMEOUT);
                                    result = COM_LIMIT;
                                } else {
                                    ym_last_filesz = filesize;
                                    ym_hdr_ok++;
                                    ym_dbg_stage = 3; ym_dbg_arg = filesize;

                                    /* Check actual parsed file size (not *p_size which is 0 here) */
                                    if (filesize > USER_FLASH_SIZE) {
                                        ym_hdr_fail++;
                                        tmp = CA;
                                        HAL_UART_Transmit(&UartHandle, &tmp, 1, NAK_TIMEOUT);
                                        HAL_UART_Transmit(&UartHandle, &tmp, 1, NAK_TIMEOUT);
                                        result = COM_LIMIT;
                                    } else {
                                        ym_dbg_stage = 4; ym_dbg_arg = filesize; ym_dbg_t0 = HAL_GetTick();
                                        ym_erase_ret = FLASH_If_Erase(USER_START_ADDRESS, filesize);
                                        ym_dbg_dt = HAL_GetTick() - ym_dbg_t0;
                                        ym_dbg_stage = 5; ym_dbg_arg = ym_erase_ret;
                                        *p_size = filesize;
                                        __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
                                        Serial_PutByte(ACK);
                                        Serial_PutByte(CRC16);
                                        ym_dbg_stage = 6; ym_dbg_arg = 0;
                                    }
                                }
                            } else {
                                /* Empty closing header after EOT. End session. */
                                Serial_PutByte(ACK);
                                file_done    = 1;
                                session_done = 1;
                                ym_dbg_stage = 11; ym_dbg_arg = packets_received;
                                break;
                            }
                        } else {
                            /* Data packet */
                            ym_dbg_stage = 7; ym_dbg_arg = packets_received;
                            ramsource = (uint32_t)&aPacketData[PACKET_DATA_INDEX];
                            if (FLASH_If_Write(flashdestination, (uint32_t *)ramsource, packet_length / 4u) == FLASHIF_OK) {
                                flashdestination += packet_length;
                                ym_pkt_ok++;
                                ym_dbg_stage = 8; ym_dbg_arg = packets_received;
                                Serial_PutByte(ACK);
                            } else {
                                ym_dbg_stage = 9; ym_dbg_arg = packets_received;
                                Serial_PutByte(CA);
                                Serial_PutByte(CA);
                                result = COM_DATA;
                            }
                        }
                        packets_received++;
                        session_begin = 1;
                    }
                    break;
                }
                break;

            case HAL_BUSY:
                Serial_PutByte(CA);
                Serial_PutByte(CA);
                result = COM_ABORT;
                break;

            default:
                if (session_begin > 0u) {
                    errors++;
                }

                /* If payload packets were already written and we now see only
                 * trailing noise/timeouts, prefer graceful session completion
                 * over repeated abort loops. */
                if ((session_begin > 0u) && (ym_pkt_ok > 0u) && (errors <= 2u)) {
                    ym_dbg_stage = 12; ym_dbg_arg = errors;
                    continue;
                }

                if (errors > MAX_ERRORS) {
                    Serial_PutByte(CA);
                    Serial_PutByte(CA);
                    result = COM_ERROR;
                } else if (session_begin > 0u) {
                    Serial_PutByte(NAK);
                } else {
                    Serial_PutByte(CRC16);
                }
                break;
            }
        }
    }

    memset(aPacketData, 0, sizeof(aPacketData));
    return result;
}

/* ===========================================================================
 * Ymodem_Transmit
 * Transmit a memory region via Ymodem-CRC16.
 * =========================================================================== */
COM_StatusTypeDef Ymodem_Transmit(uint8_t *p_buf, const uint8_t *p_file_name, uint32_t file_size)
{
    uint32_t errors = 0, ack_recpt = 0, size = 0, pkt_size;
    uint8_t *p_buf_int;
    COM_StatusTypeDef result = COM_OK;
    uint32_t blk_number = 1;
    uint8_t  a_rx_ctrl[2];
    uint8_t  i;
#ifdef CRC16_F
    uint32_t temp_crc;
#else
    uint8_t  temp_chksum;
#endif

    PrepareIntialPacket(aPacketData, p_file_name, file_size);

    while ((!ack_recpt) && (result == COM_OK)) {
        HAL_UART_Transmit(&UartHandle, &aPacketData[PACKET_START_INDEX],
                          PACKET_SIZE + PACKET_HEADER_SIZE, NAK_TIMEOUT);
#ifdef CRC16_F
        temp_crc = Cal_CRC16(&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
        Serial_PutByte((uint8_t)(temp_crc >> 8u));
        Serial_PutByte((uint8_t)(temp_crc & 0xFFu));
#else
        temp_chksum = CalcChecksum(&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
        Serial_PutByte(temp_chksum);
#endif
        if (HAL_UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK) {
            if (a_rx_ctrl[0] == ACK) {
                ack_recpt = 1;
            } else if (a_rx_ctrl[0] == CA) {
                if ((HAL_UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK) &&
                    (a_rx_ctrl[0] == CA)) {
                    HAL_Delay(2);
                    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
                    result = COM_ABORT;
                }
            }
        } else {
            errors++;
        }
        if (errors >= MAX_ERRORS) { result = COM_ERROR; }
    }

    p_buf_int = p_buf;
    size = file_size;

    while ((size > 0u) && (result == COM_OK)) {
        PreparePacket(p_buf_int, aPacketData, (uint8_t)blk_number, size);
        ack_recpt  = 0;
        a_rx_ctrl[0] = 0;
        errors = 0;

        while ((!ack_recpt) && (result == COM_OK)) {
            pkt_size = (size >= PACKET_1K_SIZE) ? PACKET_1K_SIZE : PACKET_SIZE;

            HAL_UART_Transmit(&UartHandle, &aPacketData[PACKET_START_INDEX],
                              pkt_size + PACKET_HEADER_SIZE, NAK_TIMEOUT);
#ifdef CRC16_F
            temp_crc = Cal_CRC16(&aPacketData[PACKET_DATA_INDEX], pkt_size);
            Serial_PutByte((uint8_t)(temp_crc >> 8u));
            Serial_PutByte((uint8_t)(temp_crc & 0xFFu));
#else
            temp_chksum = CalcChecksum(&aPacketData[PACKET_DATA_INDEX], pkt_size);
            Serial_PutByte(temp_chksum);
#endif
            if ((HAL_UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK) &&
                (a_rx_ctrl[0] == ACK)) {
                ack_recpt = 1;
                if (size > pkt_size) {
                    p_buf_int += pkt_size;
                    size      -= pkt_size;
                    if (blk_number == (USER_FLASH_SIZE / PACKET_1K_SIZE)) {
                        result = COM_LIMIT;
                    } else {
                        blk_number++;
                    }
                } else {
                    p_buf_int += pkt_size;
                    size = 0;
                }
            } else {
                errors++;
            }
            if (errors >= MAX_ERRORS) { result = COM_ERROR; }
        }
    }

    /* EOT */
    ack_recpt    = 0;
    a_rx_ctrl[0] = 0x00;
    errors       = 0;
    while ((!ack_recpt) && (result == COM_OK)) {
        Serial_PutByte(EOT);
        if (HAL_UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK) {
            if (a_rx_ctrl[0] == ACK) {
                ack_recpt = 1;
            } else if (a_rx_ctrl[0] == CA) {
                if ((HAL_UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK) &&
                    (a_rx_ctrl[0] == CA)) {
                    HAL_Delay(2);
                    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
                    result = COM_ABORT;
                }
            }
        } else {
            errors++;
        }
        if (errors >= MAX_ERRORS) { result = COM_ERROR; }
    }

    /* Empty closing packet */
    if (result == COM_OK) {
        aPacketData[PACKET_START_INDEX]   = SOH;
        aPacketData[PACKET_NUMBER_INDEX]  = 0;
        aPacketData[PACKET_CNUMBER_INDEX] = 0xFF;
        for (i = PACKET_DATA_INDEX; i < (PACKET_SIZE + PACKET_DATA_INDEX); i++) {
            aPacketData[i] = 0x00;
        }
        HAL_UART_Transmit(&UartHandle, &aPacketData[PACKET_START_INDEX],
                          PACKET_SIZE + PACKET_HEADER_SIZE, NAK_TIMEOUT);
#ifdef CRC16_F
        temp_crc = Cal_CRC16(&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
        Serial_PutByte((uint8_t)(temp_crc >> 8u));
        Serial_PutByte((uint8_t)(temp_crc & 0xFFu));
#else
        temp_chksum = CalcChecksum(&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
        Serial_PutByte(temp_chksum);
#endif
        if (HAL_UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK) {
            if (a_rx_ctrl[0] == CA) {
                HAL_Delay(2);
                __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
                result = COM_ABORT;
            }
        }
    }

    return result;
}
