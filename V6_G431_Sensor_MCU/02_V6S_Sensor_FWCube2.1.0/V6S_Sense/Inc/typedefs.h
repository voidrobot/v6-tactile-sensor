#ifndef TYPEDEFS_H
#define TYPEDEFS_H

#include <stdbool.h>
#include <stdint.h>

typedef int32_t  s32;
typedef int16_t  s16;
typedef int8_t   s8;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef enum
{
    TPID_UART_1 = 0x00,
    TPID_UART_2 = 0x01,
    TPID_MAX
} TERM_PORT;

#define TPID_SENSOR_LINK TPID_UART_1
#define TPID_DEBUG       TPID_UART_2

#endif
