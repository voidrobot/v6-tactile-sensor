/**
 ******************************************************************************
 * @file    can_sensor_tx.c
 * @brief   G431 sensor-side FDCAN1 TX service.
 *
 *  Reads the 3 local I2C sensors out of g_sensor[] (filled by
 *  sensor_service_i2c.c) and pushes one CanSensorPortFrame_t per
 *  CAN_SENSOR_TX_PERIOD_MS onto FDCAN1.
 *
 *  Peripheral plumbing (clock, pins, NVIC, bit timing) is owned by
 *  CubeMX-generated MX_FDCAN1_Init + HAL_FDCAN_MspInit. This module only
 *  starts the controller and pumps frames out of the foreground loop.
 *
 *  Bit timing matches H753 master (1 Mbps / 4 Mbps FD+BRS, sample 80%).
 ******************************************************************************
 */

#include "main.h"
#include "can_sensor_proto.h"
#include "sensor_service_i2c.h"
#include "prototypes.h"
#include <string.h>

extern FDCAN_HandleTypeDef hfdcan1;

/* ---------------------------------------------------------------------------
 * Diagnostic counters
 * ------------------------------------------------------------------------ */
uint32_t g_can_tx_ok          = 0;
uint32_t g_can_tx_fail        = 0;
uint32_t g_can_tx_busoff_cnt  = 0;
uint32_t g_can_tx_err_irq     = 0;
uint32_t g_can_tx_recover_cnt = 0;

static volatile uint8_t s_busoff_req = 0;

/* ---------------------------------------------------------------------------
 * can_sensor_tx_init — call once after MX_FDCAN1_Init() in main().
 *
 *  CubeMX already configured the peripheral (PCLK1 = 160 MHz kernel,
 *  Prescaler=2, Seg 63-16 / 15-4, AutoRetransmission=ENABLE).  Here we just
 *  reject any incoming traffic and start the controller. RxFifo0 is sized 0
 *  in the .ioc so we will never get an RX callback — TX only.
 * ------------------------------------------------------------------------ */
void can_sensor_tx_init(void)
{
    g_can_tx_ok          = 0;
    g_can_tx_fail        = 0;
    g_can_tx_busoff_cnt  = 0;
    g_can_tx_err_irq     = 0;
    g_can_tx_recover_cnt = 0;
    s_busoff_req         = 0;

    /* Drop everything we didn't ask for */
    (void)HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                       FDCAN_REJECT, FDCAN_REJECT,
                                       FDCAN_FILTER_REMOTE,
                                       FDCAN_FILTER_REMOTE);

    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK) {
        Error_Handler();
    }

    /* Only listen for bus-off — we never receive data on this node. */
    (void)HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_BUS_OFF, 0);
}

/* ---------------------------------------------------------------------------
 * Build one frame from the current g_sensor[] snapshot.
 * ------------------------------------------------------------------------ */
static void build_frame(CanSensorPortFrame_t *out)
{
    memset(out, 0, sizeof(*out));
    for (uint8_t i = 0U; i < SENSOR_NODE_PER_PORT; i++) {
        int32_t p = g_sensor[i].pressure_raw;     /* signed 24-bit raw */
        int16_t t = g_sensor[i].temp_raw;
        out->nodes[i].node_id  = (uint8_t)(i + 1U);
        out->nodes[i].status   = (g_sensor[i].present != 0U) ? 0U : 1U;
        out->nodes[i].press_xl = (uint8_t)( p        & 0xFFU);
        out->nodes[i].press_l  = (uint8_t)((p >> 8)  & 0xFFU);
        out->nodes[i].press_h  = (uint8_t)((p >> 16) & 0xFFU);
        out->nodes[i].temp_l   = (uint8_t)( t        & 0xFFU);
        out->nodes[i].temp_h   = (uint8_t)((t >> 8)  & 0xFFU);
    }
}

/* ---------------------------------------------------------------------------
 * can_sensor_tx_service — call every loop iteration (or 1 ms tick).
 *   - sends one frame every CAN_SENSOR_TX_PERIOD_MS
 *   - clears bus-off state when foreground sees it
 * ------------------------------------------------------------------------ */
void can_sensor_tx_service(void)
{
    static uint32_t s_last_tx = 0;

    /* Bus-off recovery (rare; controller auto-recovers but we re-arm Start) */
    if (s_busoff_req != 0U) {
        s_busoff_req = 0U;
        g_can_tx_recover_cnt++;
        (void)HAL_FDCAN_Stop(&hfdcan1);
        if (HAL_FDCAN_Start(&hfdcan1) == HAL_OK) {
            (void)HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_BUS_OFF, 0);
        }
    }

    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_last_tx) < CAN_SENSOR_TX_PERIOD_MS) {
        return;
    }
    s_last_tx = now;

    /* Skip TX if FIFO/Queue is full — avoid stalling on link disconnect. */
    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0U) {
        g_can_tx_fail++;
        return;
    }

    CanSensorPortFrame_t frame;
    build_frame(&frame);

    FDCAN_TxHeaderTypeDef tx = {0};
    tx.Identifier          = CAN_SENSOR_ID(CAN_SENSOR_THIS_PORT);
    tx.IdType              = FDCAN_STANDARD_ID;
    tx.TxFrameType         = FDCAN_DATA_FRAME;
    tx.DataLength          = FDCAN_DLC_BYTES_24;
    tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx.BitRateSwitch       = FDCAN_BRS_ON;
    tx.FDFormat            = FDCAN_FD_CAN;
    tx.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    tx.MessageMarker       = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx, (uint8_t *)&frame) == HAL_OK) {
        g_can_tx_ok++;
    } else {
        g_can_tx_fail++;
    }
}

/* ---------------------------------------------------------------------------
 * Error status callback — flag bus-off for foreground recovery.
 * ------------------------------------------------------------------------ */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan,
                                   uint32_t ErrorStatusITs)
{
    (void)hfdcan;
    g_can_tx_err_irq++;
    if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != 0U) {
        g_can_tx_busoff_cnt++;
        s_busoff_req = 1U;
    }
}
