/**
 ******************************************************************************
 * @file    can_sensor_proto.h
 * @brief   CAN FD sensor protocol (mirrors trunk-7 H753 master side).
 *
 *  This G431 is one of 5 sensor boards on a shared FDCAN1 bus. Each board
 *  aggregates 3 local I2C pressure sensors and pushes one CAN frame per
 *  CAN_SENSOR_TX_PERIOD_MS, identified by a unique standard CAN ID.
 *
 *  Bit timing matches trunk-7 H753 master side (1 Mbps / 4 Mbps FD+BRS).
 *  H753 receives 0x100..0x104; this firmware sets CAN_SENSOR_THIS_PORT to
 *  pick its slot. Change per-board before flashing.
 ******************************************************************************
 */

#ifndef CAN_SENSOR_PROTO_H
#define CAN_SENSOR_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Per-board port assignment — change before flashing each G431.
 *   0 -> CAN ID 0x100, 1 -> 0x101, ... 4 -> 0x104
 * ------------------------------------------------------------------------ */
#ifndef CAN_SENSOR_THIS_PORT
#define CAN_SENSOR_THIS_PORT       0u
#endif

#define CAN_SENSOR_ID_BASE         0x100u
#define CAN_SENSOR_ID(port)        (CAN_SENSOR_ID_BASE + (uint32_t)(port))

#define SENSOR_NODE_PER_PORT       3u

/* TX cadence (matches the legacy UART push period). */
#define CAN_SENSOR_TX_PERIOD_MS    1u

/* Per-node payload (7 bytes). SOF/CHK are dropped — CAN FD has its own CRC.
 * Pressure carries the full 24-bit LPS27HHTW raw (signed); receiver applies
 * hPa = raw / 4096. Temperature is the 16-bit raw register pair. Bytes are
 * stored in datasheet order (little-endian per field). */
typedef struct __attribute__((packed)) {
    uint8_t node_id;    /* local id, 1..SENSOR_NODE_PER_PORT */
    uint8_t status;     /* 0 = OK, 1 = sensor not present / error */
    uint8_t press_xl;   /* PRESS_OUT_XL  raw[7:0]              */
    uint8_t press_l;    /* PRESS_OUT_L   raw[15:8]             */
    uint8_t press_h;    /* PRESS_OUT_H   raw[23:16] (signed)   */
    uint8_t temp_l;     /* TEMP_OUT_L    raw[7:0]              */
    uint8_t temp_h;     /* TEMP_OUT_H    raw[15:8] (signed)    */
} CanSensorNode_t;

/* Frame: 3 nodes (21 B) + 3 B padding = 24 bytes (DLC = FDCAN_DLC_BYTES_24) */
typedef struct __attribute__((packed)) {
    CanSensorNode_t nodes[SENSOR_NODE_PER_PORT];
    uint8_t         reserved[3];
} CanSensorPortFrame_t;

#define CAN_SENSOR_PAYLOAD_BYTES   (sizeof(CanSensorPortFrame_t))   /* 24 */

#ifdef __cplusplus
}
#endif

#endif /* CAN_SENSOR_PROTO_H */
