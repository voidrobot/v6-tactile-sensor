#ifndef PROTOTYPES_H
#define PROTOTYPES_H

#include "typedefs.h"

void init_terminal_buffers(void);
void start_uart_rx_interrupt(void);
void uart_tx_service(void);
void term_uart_tx_dma_cplt(void);
void term_uart_tx_dma_error(void);
void enqueue_character(TERM_PORT tpid, char ch);
void send_to_uart_tx_queue(TERM_PORT tpid, char c);
char receive_from_uart_rx_queue(TERM_PORT tpid);
int  term_printf(TERM_PORT tpid, const char *fmt, ...);
int  trace_printf(TERM_PORT tpid, const char *fmt, ...);
void print_reset_info(void);

void init_debug_history(void);
void debug_feature_service(TERM_PORT tpid);

extern volatile uint8_t g_mon_reset_lines;
extern uint32_t g_loop_1ms_cnt;
extern uint32_t g_loop_1ms_overrun; /* */
extern uint32_t g_mon_interval_ms;
extern uint32_t g_mon_flag;
extern uint32_t g_mon_line;
extern uint32_t g_mon_onoff;
void monitor_service(TERM_PORT tpid);

/* can_sensor_tx.c — FDCAN1 TX of CanSensorPortFrame_t to H753 master */
extern uint32_t g_can_tx_ok;
extern uint32_t g_can_tx_fail;
extern uint32_t g_can_tx_busoff_cnt;
extern uint32_t g_can_tx_err_irq;
extern uint32_t g_can_tx_recover_cnt;

void can_sensor_tx_init   (void);
void can_sensor_tx_service(void);

#endif
