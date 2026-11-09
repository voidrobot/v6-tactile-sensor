/**
 * @file  ws2812.h
 * @brief WS2812B driver using TIM2_CH1 PWM DMA.
 */

#ifndef WS2812_H
#define WS2812_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ws2812_init(void);
void ws2812_set_rgb(uint8_t r, uint8_t g, uint8_t b);
uint8_t ws2812_is_busy(void);
void ws2812_set_test(uint8_t on);
uint8_t ws2812_is_test_mode(void);  /* */
void ws2812_rainbow_service(void);
void ws2812_set_hue(uint16_t hue);   /* render one hue (0..1535). By Rick 2026-06-30 */
void ws2812_boot_splash(void);       /* one blocking rainbow sweep at boot. By Rick 2026-06-30 */

#ifdef __cplusplus
}
#endif

#endif /* WS2812_H */
