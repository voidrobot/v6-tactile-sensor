#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "term_print.h"
#include "prototypes.h"

char uartTxBuffer[TPID_MAX][UART_TX_BUF_SIZE];
int uartTxHead[TPID_MAX];
int uartTxTail[TPID_MAX];
char uartRxBuffer[TPID_MAX][UART_RX_BUF_SIZE];
int uartRxHead[TPID_MAX];
int uartRxTail[TPID_MAX];
int cmd_index[TPID_MAX];
char command[TPID_MAX][COMMAND_MAX_LENGTH];
char echo_mode = 1;
uint8_t trace_onoff = 0;
uint8_t usartRxTempBuffer[TPID_MAX][2];

extern UART_HandleTypeDef huart2;

static volatile uint8_t dma_tx_busy = 0U;
static volatile uint16_t dma_tx_len = 0U;

void init_terminal_buffers(void)
{
    memset(uartTxBuffer, 0, sizeof(uartTxBuffer));
    memset(uartRxBuffer, 0, sizeof(uartRxBuffer));
    memset(uartTxHead, 0, sizeof(uartTxHead));
    memset(uartTxTail, 0, sizeof(uartTxTail));
    memset(uartRxHead, 0, sizeof(uartRxHead));
    memset(uartRxTail, 0, sizeof(uartRxTail));
    memset(cmd_index, 0, sizeof(cmd_index));
    memset(command, 0, sizeof(command));
    memset(usartRxTempBuffer, 0, sizeof(usartRxTempBuffer));
}

void start_uart_rx_interrupt(void)
{
    HAL_UART_Receive_IT(&huart2, usartRxTempBuffer[TPID_DEBUG], 1U);
}

void enqueue_character(TERM_PORT tpid, char ch)
{
    if (tpid >= TPID_MAX) { return; }
    int head = (uartRxHead[tpid] + 1) % UART_RX_BUF_SIZE;
    if (head == uartRxTail[tpid]) { return; }
    uartRxBuffer[tpid][uartRxHead[tpid]] = ch;
    uartRxHead[tpid] = head;
}

char receive_from_uart_rx_queue(TERM_PORT tpid)
{
    if (tpid >= TPID_MAX) { return (char)RX_BUFFER_EMPTY; }
    if (uartRxHead[tpid] == uartRxTail[tpid]) { return (char)RX_BUFFER_EMPTY; }
    char ch = uartRxBuffer[tpid][uartRxTail[tpid]];
    uartRxTail[tpid] = (uartRxTail[tpid] + 1) % UART_RX_BUF_SIZE;
    return ch;
}

void send_to_uart_tx_queue(TERM_PORT tpid, char c)
{
    if (tpid >= TPID_MAX) { return; }
    int head = (uartTxHead[tpid] + 1) % UART_TX_BUF_SIZE;
    if (head == uartTxTail[tpid]) { return; }
    uartTxBuffer[tpid][uartTxHead[tpid]] = c;
    uartTxHead[tpid] = head;
}

void uart_tx_service(void)
{
    if (dma_tx_busy != 0U) { return; }
    int head = uartTxHead[TPID_DEBUG];
    int tail = uartTxTail[TPID_DEBUG];
    if (head == tail) { return; }
    int chunk = (head > tail) ? (head - tail) : (UART_TX_BUF_SIZE - tail);
    dma_tx_busy = 1U;
    dma_tx_len = (uint16_t)chunk;
    if (HAL_UART_Transmit_DMA(&huart2,
                              (uint8_t *)&uartTxBuffer[TPID_DEBUG][tail],
                              (uint16_t)chunk) != HAL_OK) {
        dma_tx_busy = 0U;
        dma_tx_len = 0U;
    }
}

void term_uart_tx_dma_cplt(void)
{
    uartTxTail[TPID_DEBUG] = (uartTxTail[TPID_DEBUG] + dma_tx_len) % UART_TX_BUF_SIZE;
    dma_tx_len = 0U;
    dma_tx_busy = 0U;
}

void term_uart_tx_dma_error(void)
{
    dma_tx_busy = 0U;
    dma_tx_len = 0U;
    HAL_UART_Receive_IT(&huart2, usartRxTempBuffer[TPID_DEBUG], 1U);
}

static int vterm_printf(TERM_PORT tpid, const char *format, va_list args)
{
    char log_buf[COMMAND_MAX_LENGTH];
    int length = vsnprintf(log_buf, sizeof(log_buf), format, args);
    if (length < 0) { return 0; }
    if (length >= COMMAND_MAX_LENGTH) { length = COMMAND_MAX_LENGTH - 1; }
    for (int i = 0; i < length; i++) {
        send_to_uart_tx_queue(tpid, log_buf[i]);
    }
    return length;
}

int term_printf(TERM_PORT tpid, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vterm_printf(tpid, format, args);
    va_end(args);
    return ret;
}

int trace_printf(TERM_PORT tpid, const char *format, ...)
{
    if (trace_onoff == 0U) { return 0; }
    va_list args;
    va_start(args, format);
    int ret = vterm_printf(tpid, format, args);
    va_end(args);
    return ret;
}

void print_reset_info(void)
{
    uint32_t csr = RCC->CSR;
    term_printf(TPID_DEBUG, "\r\n\r\n==============================\r\n");
    term_printf(TPID_DEBUG, " 02_V6S_Sensor\r\n");
    term_printf(TPID_DEBUG, " UART2 VCP debug port\r\n");
    term_printf(TPID_DEBUG, "==============================\r\n");
    term_printf(TPID_DEBUG, " Reset: ");
    if ((csr & RCC_CSR_IWDGRSTF) != 0UL) { term_printf(TPID_DEBUG, "IWDG "); }
    if ((csr & RCC_CSR_SFTRSTF) != 0UL)  { term_printf(TPID_DEBUG, "SOFTWARE "); }
    if ((csr & RCC_CSR_BORRSTF) != 0UL)  { term_printf(TPID_DEBUG, "BOR "); }
    if ((csr & RCC_CSR_PINRSTF) != 0UL)  { term_printf(TPID_DEBUG, "NRST_PIN "); }
    /* Note: STM32G431 has no separate RCC_CSR_PORRSTF; POR is covered by BOR flag. */
    term_printf(TPID_DEBUG, "\r\n");
    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* Read crash type from TAMP backup register (survives soft/IWDG reset). */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE(); /* TAMP peripheral needs RTCAPB clock on STM32G431. */
    HAL_PWR_EnableBkUpAccess();
    uint32_t bkp = READ_REG(TAMP->BKP0R); /* STM32G431: TAMP->BKP0R, not RTC->BKP0R. */
    if ((bkp & 0xFF000000UL) == 0xFA000000UL) {
        const char *type = (bkp == 0xFA000001UL) ? "HARDFAULT" :
                           (bkp == 0xFA000002UL) ? "MEMMANAGE" :
                           (bkp == 0xFA000003UL) ? "BUSFAULT"  :
                           (bkp == 0xFA000004UL) ? "USAGEFAULT": "UNKNOWN";
        term_printf(TPID_DEBUG, " PrevCrash: %s\r\n", type);
        WRITE_REG(TAMP->BKP0R, 0UL); /* clear after read. */
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        enqueue_character(TPID_DEBUG, (char)usartRxTempBuffer[TPID_DEBUG][0]);
        HAL_UART_Receive_IT(&huart2, usartRxTempBuffer[TPID_DEBUG], 1U);
    }
}
