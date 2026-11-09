/**
 * @file  ws2812.c
 * @brief Single WS2812B LED driver on TIM2_CH1.
 */

#include "ws2812.h"
#include <string.h>

#define WS2812_BITS        (24U)
#define WS2812_RESET_SLOTS (48U)
#define WS2812_BUF_LEN     (WS2812_BITS + WS2812_RESET_SLOTS)
/* TIM2 counter clock = 160 MHz (PCLK1 timer clock, no APB prescaler doubling).
 * WS2812 spec  : T0H = 350 ns, T1H = 700 ns, bit period = 1.25 us.
 *   T0H_TICKS  = 0.35e-6 * 160e6 = 56 ticks  -> 350 ns
 *   T1H_TICKS  = 0.70e-6 * 160e6 = 112 ticks -> 700 ns
 *   ARR period = 1.25e-6 * 160e6 = 200 ticks (MX_TIM2_Init Period = 200-1)   */
#define WS2812_T0H_TICKS   (56U)
#define WS2812_T1H_TICKS   (112U)

extern TIM_HandleTypeDef htim2;
extern IWDG_HandleTypeDef hiwdg;   /* for boot splash watchdog refresh. By Rick 2026-06-30 */

static uint32_t         s_pwm_buf[WS2812_BUF_LEN];
static volatile uint8_t s_busy;

/* Rainbow / test mode state */
#define RAINBOW_STEP_MS    20U   /* ms between hue steps               */
#define RAINBOW_HUE_STEP   4U   /* hue units per step (0..1535 range)  */
#define RAINBOW_BRIGHTNESS 40U  /* max channel value (0-255)           */

static uint8_t  s_test_mode;
static uint16_t s_hue;           /* 0..1535  (6 sectors × 256)         */
static uint32_t s_test_tick;

/* Convert hue (0..1535) to RGB at fixed brightness */
static void hue_to_rgb(uint16_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t sector = (uint8_t)(hue >> 8U);
    uint8_t frac   = (uint8_t)(hue & 0xFFU);
    uint8_t hi = RAINBOW_BRIGHTNESS;
    uint8_t up = (uint8_t)((uint32_t)frac * RAINBOW_BRIGHTNESS / 255U);
    uint8_t dn = (uint8_t)(((uint32_t)(255U - frac) * RAINBOW_BRIGHTNESS) / 255U);

    switch (sector) {
        case 0: *r = hi; *g = up; *b = 0U; break;  /* R→Y */
        case 1: *r = dn; *g = hi; *b = 0U; break;  /* Y→G */
        case 2: *r = 0U; *g = hi; *b = up; break;  /* G→C */
        case 3: *r = 0U; *g = dn; *b = hi; break;  /* C→B */
        case 4: *r = up; *g = 0U; *b = hi; break;  /* B→M */
        default:*r = hi; *g = 0U; *b = dn; break;  /* M→R */
    }
}

void ws2812_init(void)
{
    memset(s_pwm_buf, 0, sizeof(s_pwm_buf));
    s_busy      = 0U;
    s_test_mode = 0U;
    s_hue       = 0U;
    s_test_tick = 0U;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);
}

void ws2812_set_test(uint8_t on)
{
    s_test_mode = on;
    s_hue       = 0U;
    s_test_tick = 0U;
}

uint8_t ws2812_is_test_mode(void) { return s_test_mode; }  /* */

void ws2812_rainbow_service(void)
{
    if (s_test_mode == 0U) { return; }
    if (s_busy != 0U) { return; }

    uint32_t now = HAL_GetTick();
    if ((now - s_test_tick) < RAINBOW_STEP_MS) { return; }
    s_test_tick = now;

    uint8_t r, g, b;
    hue_to_rgb(s_hue, &r, &g, &b);
    ws2812_set_rgb(r, g, b);

    s_hue = (uint16_t)(s_hue + RAINBOW_HUE_STEP);
    if (s_hue >= 1536U) { s_hue = 0U; }
}

/* Render a single hue (0..1535) at the rainbow brightness. By Rick 2026-06-30 */
void ws2812_set_hue(uint16_t hue)
{
    uint8_t r, g, b;
    hue_to_rgb(hue, &r, &g, &b);
    ws2812_set_rgb(r, g, b);
}

/* One full rainbow sweep, blocking, as a boot splash (~1.5s). Refreshes IWDG
   each step so the sweep can't trip the ~5s watchdog. By Rick 2026-06-30 */
void ws2812_boot_splash(void)
{
    for (uint16_t hue = 0U; hue < 1536U; hue += 6U) {
        ws2812_set_hue(hue);
        HAL_Delay(6U);
        HAL_IWDG_Refresh(&hiwdg);
    }
    ws2812_set_rgb(0U, 0U, 0U);  /* end dark; sensor color repaints on next tick */
}

void ws2812_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* Self-heal: a single missed DMA-complete callback would otherwise leave
       s_busy stuck at 1 and freeze the LED forever. Force-stop any prior/stuck
       transfer instead of bailing. By Rick 2026-06-30 */
    if (s_busy != 0U) {
        HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_1);
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);
        s_busy = 0U;
    }

    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
    for (uint8_t i = 0U; i < WS2812_BITS; i++) {
        uint32_t mask = (uint32_t)1UL << (23U - i);
        s_pwm_buf[i] = ((grb & mask) != 0UL) ? WS2812_T1H_TICKS : WS2812_T0H_TICKS;
    }
    for (uint8_t i = WS2812_BITS; i < WS2812_BUF_LEN; i++) {
        s_pwm_buf[i] = 0U;
    }

    s_busy = 1U;
    if (HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_1, s_pwm_buf, WS2812_BUF_LEN) != HAL_OK) {
        s_busy = 0U;
    }
}

uint8_t ws2812_is_busy(void)
{
    return s_busy;
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_1);
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, 0U);
        s_busy = 0U;
    }
}
