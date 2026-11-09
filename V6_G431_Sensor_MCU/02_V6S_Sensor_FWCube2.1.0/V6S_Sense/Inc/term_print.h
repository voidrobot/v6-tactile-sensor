#ifndef TERM_PRINT_H
#define TERM_PRINT_H

#include <stdint.h>
#include "typedefs.h"

#define UART_RX_BUF_SIZE        (1024)
#define UART_TX_BUF_SIZE        (2048)
#define RX_BUFFER_EMPTY         (0)

#define COMMAND_MAX_LENGTH      (160)
#define MAX_CMD_ARGV            (10)
#define MAX_ARGV_LENGTH         (24)

extern int uartTxHead[TPID_MAX];
extern int uartTxTail[TPID_MAX];
extern int uartRxHead[TPID_MAX];
extern int uartRxTail[TPID_MAX];
extern int cmd_index[TPID_MAX];
extern char command[TPID_MAX][COMMAND_MAX_LENGTH];
extern char echo_mode;
extern uint8_t trace_onoff;
extern uint8_t usartRxTempBuffer[TPID_MAX][2];

#endif
