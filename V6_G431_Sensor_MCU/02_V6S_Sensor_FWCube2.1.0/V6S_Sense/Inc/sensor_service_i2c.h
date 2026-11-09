/**
 * @file  sensor_service_i2c.h
 * @brief 3-channel LPS27HHTW sensor service and USART1 DMA push framing.
 */

#ifndef SENSOR_SERVICE_I2C_H
#define SENSOR_SERVICE_I2C_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_CH_NUM              (3U)

/* ---------------------------------------------------------------------------
 * Sensor transport selector (must match trunk-7 H753 master).
 *   SENSOR_TRANSPORT_UART : legacy UART1 DMA push (33-byte frame)
 *   SENSOR_TRANSPORT_CAN  : FDCAN1 (PA11/PA12), 1 frame per port (24 bytes FD+BRS)
 *   SENSOR_TRANSPORT_BOTH : both paths active in parallel (bring-up)
 *   SENSOR_TRANSPORT_DXL  : Dynamixel Protocol 2.0 slave on USART1
 * Override on the per-board build command line with -DSENSOR_TRANSPORT=...   */
#define SENSOR_TRANSPORT_UART      0u
#define SENSOR_TRANSPORT_CAN       1u
#define SENSOR_TRANSPORT_BOTH      2u
#define SENSOR_TRANSPORT_DXL       3u

#ifndef SENSOR_TRANSPORT
#define SENSOR_TRANSPORT           SENSOR_TRANSPORT_DXL
#endif

#define SENSOR_TRANSPORT_USE_UART  (((SENSOR_TRANSPORT) == SENSOR_TRANSPORT_UART) || \
                                    ((SENSOR_TRANSPORT) == SENSOR_TRANSPORT_BOTH))
#define SENSOR_TRANSPORT_USE_CAN   (((SENSOR_TRANSPORT) == SENSOR_TRANSPORT_CAN)  || \
                                    ((SENSOR_TRANSPORT) == SENSOR_TRANSPORT_BOTH))
#define SENSOR_TRANSPORT_USE_DXL   ((SENSOR_TRANSPORT) == SENSOR_TRANSPORT_DXL)

#define SENSOR_I2C_ADDR_CH0        (0x5CU)
#define SENSOR_I2C_ADDR_CH1        (0x5CU)
#define SENSOR_I2C_ADDR_CH2        (0x5CU)

#define SENSOR_UART_SOF            (0xA5U)
#define SENSOR_UART_NODE_LEN       (11U) /* SOF,NODE_ID,TYPE,P_XL,P_L,P_H,T_L,T_H,ERR,RSV,XOR */
#define SENSOR_UART_RESP_LEN       (SENSOR_CH_NUM * SENSOR_UART_NODE_LEN) /* 33 bytes */
#define SENSOR_UART_PUSH_PERIOD_MS (1U)
#define SENSOR_POLL_MS             (5U)

typedef enum
{
    SS_IDLE = 0,
    SS_BUSY,
    SS_ERROR,
    SS_RECOVERY
} sensor_state_t;

typedef struct
{
    volatile sensor_state_t state;
    uint8_t  present;
    uint8_t  data_valid;
    uint8_t  use_fake;
    uint8_t  warmup;         /* 1 = next completed sample is the first one after
                               * (re)config and must be discarded (LPS27HH ODR
                               * conversion may still be in flight); cleared
                               * after that first sample is consumed. */
    uint8_t  rx_raw[5];
    int32_t  pressure_raw;
    uint16_t value_hpa;
    int16_t  temp_raw;
    int16_t  temp_c_x100;
    uint32_t sample_cnt;
    uint32_t start_cnt;
    uint32_t err_cnt;
    uint32_t last_i2c_err;
    uint32_t last_tick;
    uint32_t busy_start_tick; /* HAL_GetTick() when SS_BUSY was entered; used
                                * to detect a DMA transfer that never calls
                                * back (bus wedged by noise). */
} sensor_ch_t;

extern sensor_ch_t       g_sensor[SENSOR_CH_NUM];
extern volatile uint8_t  g_sensor_uart_busy;
extern volatile uint32_t g_sen1_req_count;
extern volatile uint32_t g_sen1_rsp_count;
extern volatile uint32_t g_sen1_drop_count;
extern volatile uint32_t g_sen1_other_count;
extern volatile uint32_t g_sen1_other_id_count;    /* other id. */
extern volatile uint32_t g_sen1_noise_count;       /* chunk noise. */

void sensor_service_i2c_init(void);
void sensor_service_i2c_poll(void);
void sensor_service_build_uart_frame(void);

HAL_StatusTypeDef sensor_service_i2c_test(uint8_t ch, uint8_t out[5]);
uint8_t           sensor_service_i2c_scan(uint8_t ch, uint8_t result[16]);
uint8_t           sensor_service_i2c_get_addr(uint8_t ch); /* runtime-resolved 7-bit addr (0x5C/0x5D); 0 if ch invalid */
uint8_t           sensor_service_i2c_is_faulty(uint8_t ch); /* 1 if parked as declared-faulty (lock-up, no power-cycle fix available) */
HAL_StatusTypeDef sensor_service_i2c_read_status(uint8_t ch, uint8_t *status); /* debug-only: raw STATUS(0x27h) */
HAL_StatusTypeDef sensor_service_i2c_read_reg(uint8_t ch, uint8_t reg, uint8_t *out); /* debug-only: raw read of any register */
HAL_StatusTypeDef sensor_service_i2c_set_odr(uint8_t ch, uint8_t odr_code); /* debug-only: live ODR change, 0..7 (see LPS27HH_ODR_CODE_* in sensor_service_i2c.c) */
#define SENSOR_I2C_STATUS_P_DA (0x01U)
#define SENSOR_I2C_STATUS_T_DA (0x02U)
HAL_StatusTypeDef sensor_service_i2c_swreset(uint8_t ch); /* debug-only: SWRESET the sensor + re-probe/config */
HAL_StatusTypeDef sensor_service_i2c_boot(uint8_t ch); /* debug-only: reload factory trim (BOOT) + re-probe/config */

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_SERVICE_I2C_H */
