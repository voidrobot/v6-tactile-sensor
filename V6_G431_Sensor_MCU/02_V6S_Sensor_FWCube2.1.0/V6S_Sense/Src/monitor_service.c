#include "main.h"
#include "sensor_service_i2c.h"
#include "term_print.h"
#include "prototypes.h"
#include "env_flash.h"

extern FDCAN_HandleTypeDef hfdcan1;
extern volatile uint32_t g_dxl_ore_count;
extern volatile uint32_t g_dxl_ore_full_buf_count;
extern volatile uint32_t g_dxl_ore_during_tx_count;
extern volatile uint32_t g_uart1_err_count;
extern volatile uint32_t g_uart1_last_err;
extern volatile uint32_t g_uart2_err_count;
extern volatile uint32_t g_uart2_last_err;

volatile uint8_t g_mon_reset_lines;
uint32_t g_loop_1ms_cnt;
uint32_t g_loop_1ms_overrun; /* */
uint32_t g_mon_interval_ms = 1000U;
uint32_t g_mon_flag = 0x02U;
uint32_t g_mon_line = 1U;
uint32_t g_mon_onoff = 1U;

static uint32_t s_mon_last_tick;
static uint32_t s_last_loop_cnt;

void monitor_service(TERM_PORT tpid)
{
    uint32_t now = HAL_GetTick();
    g_mon_onoff = env_working[PARM_MON_ONOFF];
    if (g_mon_onoff == 0U) { return; }
    if (cmd_index[TPID_DEBUG] > 0) { return; }

    if (env_working[PARM_MON_INTERVAL_SEC] == 0U) { env_working[PARM_MON_INTERVAL_SEC] = 1U; }
    g_mon_interval_ms = env_working[PARM_MON_INTERVAL_SEC] * 1000U;
    g_mon_flag = env_working[PARM_MON_FLAG];
    g_mon_line = env_working[PARM_MON_LINE];

    if ((now - s_mon_last_tick) < g_mon_interval_ms) { return; }
    s_mon_last_tick = now;

    if ((g_mon_flag & 0x01U) != 0U) {
        term_printf(tpid, "[SYS] tick=%lu loop=%lu dloop=%lu overrun=%lu\r\n",
                    (unsigned long)now,
                    (unsigned long)g_loop_1ms_cnt,
                    (unsigned long)(g_loop_1ms_cnt - s_last_loop_cnt),
                    (unsigned long)g_loop_1ms_overrun);
        s_last_loop_cnt = g_loop_1ms_cnt;
    }
    if ((g_mon_flag & 0x02U) != 0U) {
        term_printf(tpid,
                    "[SEN] p=%u,%u,%u  t=%d,%d,%d  present=%u,%u,%u data_err=%u,%u,%u sample=%lu,%lu,%lu err_cnt=%lu,%lu,%lu uart_busy=%u\r\n",
                    (unsigned int)g_sensor[0].value_hpa,
                    (unsigned int)g_sensor[1].value_hpa,
                    (unsigned int)g_sensor[2].value_hpa,
                    (int)g_sensor[0].temp_c_x100,
                    (int)g_sensor[1].temp_c_x100,
                    (int)g_sensor[2].temp_c_x100,
                    (unsigned int)g_sensor[0].present,
                    (unsigned int)g_sensor[1].present,
                    (unsigned int)g_sensor[2].present,
                    (unsigned int)((g_sensor[0].data_valid != 0U) ? 0U : 1U),
                    (unsigned int)((g_sensor[1].data_valid != 0U) ? 0U : 1U),
                    (unsigned int)((g_sensor[2].data_valid != 0U) ? 0U : 1U),
                    (unsigned long)g_sensor[0].sample_cnt,
                    (unsigned long)g_sensor[1].sample_cnt,
                    (unsigned long)g_sensor[2].sample_cnt,
                    (unsigned long)g_sensor[0].err_cnt,
                    (unsigned long)g_sensor[1].err_cnt,
                    (unsigned long)g_sensor[2].err_cnt,
                    (unsigned int)g_sensor_uart_busy);
    }
    if ((g_mon_flag & 0x04U) != 0U) {
        term_printf(tpid, "[UART1] req=%lu rsp=%lu drop=%lu other=%lu oid=%lu noise=%lu\r\n", /* diag split. */
                    (unsigned long)g_sen1_req_count,
                    (unsigned long)g_sen1_rsp_count,
                    (unsigned long)g_sen1_drop_count,
                    (unsigned long)g_sen1_other_count,
                    (unsigned long)g_sen1_other_id_count,
                    (unsigned long)g_sen1_noise_count);
        term_printf(tpid, "[UART1 DXL DIAG] ore=%lu full_buf=%lu tx_active=%lu\r\n", /* moved out of ISR - see sensor_dxl_uart_error(). */
                    (unsigned long)g_dxl_ore_count,
                    (unsigned long)g_dxl_ore_full_buf_count,
                    (unsigned long)g_dxl_ore_during_tx_count);
        term_printf(tpid, "[UART ERR] u1_cnt=%lu u1_last=0x%lx u2_cnt=%lu u2_last=0x%lx\r\n", /* moved out of ISR - see HAL_UART_ErrorCallback(). */
                    (unsigned long)g_uart1_err_count,
                    (unsigned long)g_uart1_last_err,
                    (unsigned long)g_uart2_err_count,
                    (unsigned long)g_uart2_last_err);
    }
    if ((g_mon_flag & 0x08U) != 0U) {
        FDCAN_ProtocolStatusTypeDef ps = {0};
        FDCAN_ErrorCountersTypeDef ec = {0};
        (void)HAL_FDCAN_GetProtocolStatus(&hfdcan1, &ps);
        (void)HAL_FDCAN_GetErrorCounters(&hfdcan1, &ec);

        term_printf(tpid,
                    "[CAN] tx_ok=%lu fail=%lu irq=%lu busoff=%lu recov=%lu state=%u ps(le=%lu dle=%lu act=%lu ep=%lu warn=%lu bo=%lu) ec(tx=%lu rx=%lu log=%lu) txf_free=%lu\r\n",
                    (unsigned long)g_can_tx_ok,
                    (unsigned long)g_can_tx_fail,
                    (unsigned long)g_can_tx_err_irq,
                    (unsigned long)g_can_tx_busoff_cnt,
                    (unsigned long)g_can_tx_recover_cnt,
                    (unsigned int)HAL_FDCAN_GetState(&hfdcan1),
                    (unsigned long)ps.LastErrorCode,
                    (unsigned long)ps.DataLastErrorCode,
                    (unsigned long)ps.Activity,
                    (unsigned long)ps.ErrorPassive,
                    (unsigned long)ps.Warning,
                    (unsigned long)ps.BusOff,
                    (unsigned long)ec.TxErrorCnt,
                    (unsigned long)ec.RxErrorCnt,
                    (unsigned long)ec.ErrorLogging,
                    (unsigned long)HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1));
        term_printf(tpid,
                    "[CAN] regs CCCR=%08lX NBTP=%08lX DBTP=%08lX PA_MODER=%08lX PA_AFRH=%08lX PA_IDR=%08lX\r\n",
                    (unsigned long)hfdcan1.Instance->CCCR,
                    (unsigned long)hfdcan1.Instance->NBTP,
                    (unsigned long)hfdcan1.Instance->DBTP,
                    (unsigned long)GPIOA->MODER,
                    (unsigned long)GPIOA->AFR[1],
                    (unsigned long)GPIOA->IDR);
        term_printf(tpid,
                    "[CAN] fdcan_kclk=%lu Hz\r\n",
                    (unsigned long)HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN));


    }
}
