/**
 * @file sensor_dxl_service.h
 * @brief Dynamixel Protocol 2.0 slave transport for the 3-channel sensor board.
 */

#ifndef SENSOR_DXL_SERVICE_H
#define SENSOR_DXL_SERVICE_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_DXL_ID_BASE          14U   /* bus-local sensor id. */
#define SENSOR_DXL_REG_DATA_BASE    0x0030U
#define SENSOR_DXL_REG_DATA_LEN     10U
#define SENSOR_DXL_REG_TOTAL_LEN    (SENSOR_CH_NUM * SENSOR_DXL_REG_DATA_LEN) /* ch map. */

void sensor_dxl_service_init(void);
void sensor_dxl_service_poll(void);
void sensor_dxl_uart_rx_event(UART_HandleTypeDef *huart, uint16_t size);
void sensor_dxl_uart_tx_complete(UART_HandleTypeDef *huart);
void sensor_dxl_uart_error(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_DXL_SERVICE_H */
