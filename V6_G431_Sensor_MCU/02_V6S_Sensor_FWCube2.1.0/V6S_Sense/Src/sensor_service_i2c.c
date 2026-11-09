/**
 * @file  sensor_service_i2c.c
 * @brief LPS27HHTW I2C DMA reader and USART1 DMA push service.
 */

#include "sensor_service_i2c.h"
#include "sensor_dxl_service.h"
#include "main.h"
#include "prototypes.h"
#include "term_print.h"
#include <string.h>

#define LPS27HH_ADDR_SHIFTED(addr) ((uint16_t)((uint16_t)(addr) << 1U))
#define LPS27HH_WHO_AM_I          (0x0FU)
#define LPS27HH_CTRL_REG1         (0x10U)
#define LPS27HH_CTRL_REG2         (0x11U)
#define LPS27HH_PRESS_OUT_XL      (0x28U)
#define LPS27HH_STATUS            (0x27U)
#define LPS27HH_STATUS_P_DA       (0x01U) /* pressure data available */
#define LPS27HH_STATUS_T_DA       (0x02U) /* temperature data available */
#define LPS27HH_WHO_AM_I_VALUE    (0xB3U)
#define LPS27HH_CTRL2_INIT        (0x10U) /* IF_ADD_INC=1, LOW_NOISE_EN=0 (must be 0 at ODR=100/200Hz per ST AN5590) */
#define LPS27HH_CTRL2_SWRESET     (0x04U) /* SWRESET bit in CTRL_REG2: resets all sensor regs to power-on default, self-clearing */
#define LPS27HH_CTRL2_BOOT        (0x80U) /* BOOT bit in CTRL_REG2: reloads factory trimming/calibration from NVM, self-clearing */
#define LPS27HH_CTRL1_INIT        (0x72U) /* 200 Hz max ODR, BDU=1 */

/* ODR[2:0] field is CTRL_REG1 bits[6:4]. Values below are for live debug
 * testing only (I2C ODR CLI command) - LPS27HH_CTRL1_INIT above is what's
 * actually used at boot/reprobe. Keep BDU=1 (bit1) and LOW_NOISE_EN already
 * off (CTRL2) regardless of ODR chosen here. */
#define LPS27HH_ODR_CODE_1HZ    (0x01U)
#define LPS27HH_ODR_CODE_10HZ   (0x02U)
#define LPS27HH_ODR_CODE_25HZ   (0x03U)
#define LPS27HH_ODR_CODE_50HZ   (0x04U)
#define LPS27HH_ODR_CODE_75HZ   (0x05U)
#define LPS27HH_ODR_CODE_100HZ  (0x06U)
#define LPS27HH_ODR_CODE_200HZ  (0x07U)

#define SENSOR_ODR_VERIFY_RETRY_LIMIT (5U) /* write+readback attempts for sensor_service_i2c_set_odr();
                                            * ODR writes were confirmed to sometimes be silently
                                            * ignored by the sensor even though the I2C transaction
                                            * itself reports success. */

/* I2C error recovery thresholds */
#define I2C_CONSEC_ERR_LIMIT      10U    /* consecutive errors before bus reset */
#define I2C_RECOVERY_BACKOFF_MS   1000U  /* ms between recovery attempts */
#define I2C_BUSY_TIMEOUT_MS       (10U * SENSOR_POLL_MS) /* max time a DMA
                                    * read may stay SS_BUSY with neither
                                    * MemRxCplt nor Error callback firing
                                    * (bus wedged by noise/clock-stretch) */

extern I2C_HandleTypeDef  hi2c1;
extern I2C_HandleTypeDef  hi2c2;
extern I2C_HandleTypeDef  hi2c3;
extern UART_HandleTypeDef huart1;

sensor_ch_t       g_sensor[SENSOR_CH_NUM];
volatile uint8_t  g_sensor_uart_busy;
volatile uint32_t g_sen1_req_count;
volatile uint32_t g_sen1_rsp_count;
volatile uint32_t g_sen1_drop_count;
volatile uint32_t g_sen1_other_count;
volatile uint32_t g_sen1_other_id_count;    /* other id. */
volatile uint32_t g_sen1_noise_count;       /* chunk noise. */
static uint8_t    s_uart_tx_buf[SENSOR_UART_RESP_LEN];

static uint32_t s_last_push_tick;

/* Per-channel consecutive error tracking (foreground + ISR, both write) */
static volatile uint32_t s_consec_err[SENSOR_CH_NUM];
static uint32_t          s_recovery_tick[SENSOR_CH_NUM];


static I2C_HandleTypeDef *sensor_i2c(uint8_t ch)
{
    if (ch == 0U) { return &hi2c1; }
    if (ch == 1U) { return &hi2c2; }
    return &hi2c3;
}

/* Runtime I2C address per channel. Starts at the compiled default (SENSOR_I2C_ADDR_CHn)
 * and is overridden by sensor_probe_and_config() if that channel's SA0 strap
 * turns out to answer at the other LPS27HHTW address instead (0x5C vs 0x5D).
 * Re-initialized to the defaults on every reset via normal static init. */
static uint8_t s_i2c_addr[SENSOR_CH_NUM] = {
    SENSOR_I2C_ADDR_CH0,
    SENSOR_I2C_ADDR_CH1,
    SENSOR_I2C_ADDR_CH2,
};

static uint8_t sensor_addr(uint8_t ch)
{
    return s_i2c_addr[ch];
}

uint8_t sensor_service_i2c_get_addr(uint8_t ch)
{
    if (ch >= SENSOR_CH_NUM) { return 0U; }
    return s_i2c_addr[ch];
}

/* LPS27HHTW only has two possible 7-bit addresses, selected by the SA0 pin
 * (0x5C/0x5D differ only in bit0). */
static uint8_t lps27hh_alt_addr(uint8_t addr7)
{
    return (uint8_t)(addr7 ^ 0x01U);
}

static int8_t sensor_ch_from_i2c(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) { return 0; }
    if (hi2c->Instance == I2C2) { return 1; }
    if (hi2c->Instance == I2C3) { return 2; }
    return -1;
}

static uint8_t xor_n(const uint8_t *p, uint8_t n)
{
    uint8_t cs = 0U;
    for (uint8_t i = 0U; i < n; i++) {
        cs ^= p[i];
    }
    return cs;
}

static int32_t sign_extend24(uint32_t v)
{
    if ((v & 0x00800000UL) != 0UL) {
        v |= 0xFF000000UL;
    }
    return (int32_t)v;
}

static void sensor_fake_update(uint8_t ch)
{
    static uint16_t s_fake_ctr[SENSOR_CH_NUM];
    s_fake_ctr[ch] = (uint16_t)((s_fake_ctr[ch] + 1U) & 0x1FFFU);

    int32_t base = (int32_t)(1013) * 4096;
    int32_t swing = (int32_t)(s_fake_ctr[ch]);
    if (swing > 4095) { swing = 8191 - swing; }

    sensor_ch_t *s = &g_sensor[ch];
    s->pressure_raw  = base + swing;
    s->value_hpa     = (uint16_t)(s->pressure_raw / 4096);
    s->temp_raw      = 2500;
    s->temp_c_x100   = 2500;
    s->present       = 1U;
    s->data_valid    = 1U;
    s->sample_cnt++;
    s->state         = SS_IDLE;
}

static void sensor_clear_data(uint8_t ch)
{
    memset(g_sensor[ch].rx_raw, 0, sizeof(g_sensor[ch].rx_raw));
    g_sensor[ch].data_valid   = 0U;
    g_sensor[ch].pressure_raw = 0;
    g_sensor[ch].value_hpa    = 0U;
    g_sensor[ch].temp_raw     = 0;
    g_sensor[ch].temp_c_x100  = 0;
}

/* s_consec_err[] is written from both the foreground poll loop and I2C ISR
 * callbacks (HAL_I2C_MemRxCpltCallback / HAL_I2C_ErrorCallback). A plain
 * "x++" is a non-atomic read-modify-write on Cortex-M; if an ISR increments
 * the same element while the foreground loop is mid-increment, one update
 * can be lost. Wrap all accesses in a short critical section. */
static void sensor_err_inc(uint8_t ch)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_consec_err[ch]++;
    __set_PRIMASK(primask);
}

static void sensor_err_reset(uint8_t ch)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_consec_err[ch] = 0U;
    __set_PRIMASK(primask);
}

static uint32_t sensor_err_get(uint8_t ch)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t v;
    __disable_irq();
    v = s_consec_err[ch];
    __set_PRIMASK(primask);
    return v;
}

static void sensor_enter_recovery(uint8_t ch, uint32_t now)
{
    sensor_ch_t *s = &g_sensor[ch];
    s->present = 0U;
    s->use_fake = 0U;
    sensor_clear_data(ch);
    s->state = SS_RECOVERY;
    s_recovery_tick[ch] = now;
    sensor_err_reset(ch);
}

typedef struct { GPIO_TypeDef *scl_port; uint16_t scl_pin; GPIO_TypeDef *sda_port; uint16_t sda_pin; } i2c_pin_t;
static const i2c_pin_t s_i2c_pins[SENSOR_CH_NUM] = {
    { GPIOA, GPIO_PIN_15, GPIOB, GPIO_PIN_7 },  /* I2C1: PA15=SCL, PB7=SDA  */
    { GPIOA, GPIO_PIN_9,  GPIOF, GPIO_PIN_0 },  /* I2C2: PA9=SCL,  PF0=SDA  */
    { GPIOA, GPIO_PIN_8,  GPIOB, GPIO_PIN_5 },  /* I2C3: PA8=SCL,  PB5=SDA  */
};

/* Lockup detection: an analog front-end lock-up reads back as a completely
 * fixed raw pressure+temperature across many otherwise-successful samples
 * (confirmed on real units: sample_cnt kept climbing while raw/temp never
 * moved a single bit). A plain reconfigure doesn't clear it, but a validated
 * ODR-change + SWRESET + BOOT sequence (see sensor_service_i2c_poll()) does,
 * on a channel resolving its normal I2C address. If repeated attempts still
 * fail (e.g. a channel stuck on the alternate address - a physical SA0/GND
 * contact issue no register sequence can fix), the channel is parked
 * (present forced 0) rather than keep handing out a frozen bad reading as if
 * it were valid. */
#define SENSOR_LOCKUP_SOFT_RETRY_LIMIT (3U) /* digital re-init attempts before giving up and
                                             * declaring the channel faulty - best-effort only,
                                             * not guaranteed to clear a true analog lock-up. */

/* Cross-sensor plausibility check: compares this channel's temperature
 * against its peers'. 50.00 degC is deliberately generous - real grip/
 * ambient temperature spread between fingers should never approach it, but
 * the lock-ups seen so far (170C, -257C) are far beyond it. */
#define SENSOR_LOCKUP_TEMP_DEV_LIMIT_C100    (5000)  /* 50.00 degC, in temp_c_x100 units */
#define SENSOR_LOCKUP_TEMP_DEV_REPEAT_LIMIT  (40U)   /* ~200ms at 5ms poll */
static uint32_t s_temp_dev_cnt[SENSOR_CH_NUM];

/* Hard absolute-implausibility check: temp above 65C is not achievable in
 * this application (grip/ambient). Pressure below 500 hPa is likewise not
 * achievable - normal resting atmospheric pressure for this setup is ~1000
 * hPa, and grip events push it up (already confirmed to legitimately exceed
 * 1260 hPa), never down anywhere near 500. Both are near-unambiguous, so a
 * short dwell (vs. the 200ms above) is enough to rule out a one-off
 * transient glitch. */
#define SENSOR_LOCKUP_TEMP_ABS_LIMIT_C100 (6500) /* 65.00 degC */
#define SENSOR_LOCKUP_HPA_MIN             (500U) /* hPa */
#define SENSOR_LOCKUP_HARD_REPEAT_LIMIT   (5U)   /* ~25ms at 5ms poll */
static uint32_t s_hard_implausible_cnt[SENSOR_CH_NUM];
static uint32_t s_soft_retry_cnt[SENSOR_CH_NUM];
static volatile uint8_t s_declared_faulty[SENSOR_CH_NUM]; /* parked: present forced 0, polling
                                                            * skipped, until a manual SWRESET/BOOT
                                                            * clears it. */
static volatile uint8_t s_lockup_pending[SENSOR_CH_NUM]; /* set by ISR, serviced by poll() (foreground) */

uint8_t sensor_service_i2c_is_faulty(uint8_t ch)
{
    return (ch < SENSOR_CH_NUM) ? s_declared_faulty[ch] : 0U;
}

/* Operator-facing only: explicitly clears the parked/faulty state and all
 * lock-up-tracking counters for a channel. Called by the manual SWRESET/BOOT
 * CLI commands after a successful call, signalling "an operator just acted
 * on this, trust it fresh". Must NOT be called from the automatic recovery
 * sequence in sensor_service_i2c_poll() - that sequence calls the same
 * underlying SWRESET/BOOT functions on every retry attempt, and clearing
 * s_soft_retry_cnt on every one of ITS OWN successful register writes would
 * make the retry count unable to ever reach SENSOR_LOCKUP_SOFT_RETRY_LIMIT. */
void sensor_service_i2c_clear_faulty(uint8_t ch)
{
    if (ch >= SENSOR_CH_NUM) { return; }
    s_declared_faulty[ch]      = 0U;
    s_soft_retry_cnt[ch]       = 0U;
    s_temp_dev_cnt[ch]         = 0U;
    s_hard_implausible_cnt[ch] = 0U;
}

static void i2c_delay_10us(void)
{
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    }
    uint32_t end = DWT->CYCCNT + (10U * (SystemCoreClock / 1000000U));
    while ((int32_t)(DWT->CYCCNT - end) < 0) { __NOP(); }
}

static void i2c_bus_recover(uint8_t ch)
{
    const i2c_pin_t *p = &s_i2c_pins[ch];
    GPIO_InitTypeDef g = { 0 };

    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin   = p->scl_pin;  HAL_GPIO_Init(p->scl_port, &g);
    g.Pin   = p->sda_pin;  HAL_GPIO_Init(p->sda_port, &g);

    HAL_GPIO_WritePin(p->scl_port, p->scl_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(p->sda_port, p->sda_pin, GPIO_PIN_SET);
    i2c_delay_10us();

    for (uint8_t i = 0U; i < 9U; i++) {
        HAL_GPIO_WritePin(p->scl_port, p->scl_pin, GPIO_PIN_RESET);
        i2c_delay_10us();
        HAL_GPIO_WritePin(p->scl_port, p->scl_pin, GPIO_PIN_SET);
        i2c_delay_10us();
        if (HAL_GPIO_ReadPin(p->sda_port, p->sda_pin) == GPIO_PIN_SET) { break; }
    }

    HAL_GPIO_WritePin(p->sda_port, p->sda_pin, GPIO_PIN_RESET);
    i2c_delay_10us();
    HAL_GPIO_WritePin(p->scl_port, p->scl_pin, GPIO_PIN_SET);
    i2c_delay_10us();
    HAL_GPIO_WritePin(p->sda_port, p->sda_pin, GPIO_PIN_SET);
    i2c_delay_10us();
}

/* Immediate hard reset (DeInit + bus-clock-out) followed by entering the
 * normal backoff/recovery state. Only safe to call from foreground context
 * (poll loop) - never from an I2C ISR callback. Used when we detect the
 * bus is actually wedged (stuck SS_BUSY with no callback at all), as
 * opposed to a HAL-reported error where HAL has already unwound the
 * transfer and the existing backoff-driven reset in the SS_RECOVERY
 * branch is enough. */
static void sensor_force_reset_and_recover(uint8_t ch, uint32_t now)
{
    I2C_HandleTypeDef *hi2c = sensor_i2c(ch);
    (void)HAL_I2C_DeInit(hi2c);
    i2c_bus_recover(ch);
    sensor_enter_recovery(ch, now);
}

static HAL_StatusTypeDef sensor_write_reg(uint8_t ch, uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(sensor_i2c(ch),
                             LPS27HH_ADDR_SHIFTED(sensor_addr(ch)),
                             reg, I2C_MEMADD_SIZE_8BIT, &val, 1U, 20U);
}

static HAL_StatusTypeDef sensor_probe_and_config(uint8_t ch)
{
    uint8_t who = 0U;
    I2C_HandleTypeDef *hi2c = sensor_i2c(ch);

    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(hi2c,
                                             LPS27HH_ADDR_SHIFTED(sensor_addr(ch)),
                                             LPS27HH_WHO_AM_I, I2C_MEMADD_SIZE_8BIT,
                                             &who, 1U, 20U);
    if ((ret != HAL_OK) || (who != LPS27HH_WHO_AM_I_VALUE)) {
        /* Current address didn't answer as a valid LPS27HHTW - check whether
         * this channel's SA0 strap actually resolves to the other address
         * (e.g. a marginal/open SA0-to-GND connection floating high). Only
         * adopt it if WHO_AM_I confirms it's really the sensor, not just
         * "something ACKed" - avoids mistaking bus noise or an unrelated
         * device for the sensor. */
        uint8_t alt = lps27hh_alt_addr(sensor_addr(ch));
        uint8_t alt_who = 0U;
        HAL_StatusTypeDef alt_ret = HAL_I2C_Mem_Read(hi2c,
                                                     LPS27HH_ADDR_SHIFTED(alt),
                                                     LPS27HH_WHO_AM_I, I2C_MEMADD_SIZE_8BIT,
                                                     &alt_who, 1U, 20U);
        if ((alt_ret == HAL_OK) && (alt_who == LPS27HH_WHO_AM_I_VALUE)) {
            s_i2c_addr[ch] = alt;
        } else {
            g_sensor[ch].last_i2c_err = HAL_I2C_GetError(hi2c);
            return HAL_ERROR;
        }
    }

    ret = sensor_write_reg(ch, LPS27HH_CTRL_REG2, LPS27HH_CTRL2_INIT);
    if (ret == HAL_OK) {
        ret = sensor_write_reg(ch, LPS27HH_CTRL_REG1, LPS27HH_CTRL1_INIT);
    }
    if (ret != HAL_OK) {
        g_sensor[ch].last_i2c_err = HAL_I2C_GetError(hi2c);
    }
    return ret;
}

void sensor_service_build_uart_frame(void)
{
    for (uint8_t ch = 0U; ch < SENSOR_CH_NUM; ch++) {
        uint8_t  base = (uint8_t)(ch * SENSOR_UART_NODE_LEN);
        int32_t  p    = g_sensor[ch].pressure_raw;
        int16_t  t    = g_sensor[ch].temp_raw;

        s_uart_tx_buf[base + 0U]  = SENSOR_UART_SOF;
        s_uart_tx_buf[base + 1U]  = (uint8_t)(ch + 1U);
        s_uart_tx_buf[base + 2U]  = 0x02U;
        s_uart_tx_buf[base + 3U]  = (uint8_t)( p        & 0xFFU);
        s_uart_tx_buf[base + 4U]  = (uint8_t)((p >> 8)  & 0xFFU);
        s_uart_tx_buf[base + 5U]  = (uint8_t)((p >> 16) & 0xFFU);
        s_uart_tx_buf[base + 6U]  = (uint8_t)( t        & 0xFFU);
        s_uart_tx_buf[base + 7U]  = (uint8_t)((t >> 8)  & 0xFFU);
        s_uart_tx_buf[base + 8U]  = (g_sensor[ch].data_valid != 0U) ? 0x00U : 0x01U;
        s_uart_tx_buf[base + 9U]  = 0x00U;
        s_uart_tx_buf[base + 10U] = xor_n(&s_uart_tx_buf[base], 10U);
    }
}

void sensor_service_i2c_init(void)
{
    memset(g_sensor, 0, sizeof(g_sensor));
    memset(s_uart_tx_buf, 0, sizeof(s_uart_tx_buf));
    g_sensor_uart_busy    = 0U;
    g_sen1_req_count      = 0U;
    g_sen1_rsp_count      = 0U;
    g_sen1_drop_count     = 0U;
    g_sen1_other_count    = 0U;
    g_sen1_other_id_count = 0U;
    g_sen1_noise_count    = 0U;
    memset((void *)s_consec_err, 0, sizeof(s_consec_err));
    memset(s_recovery_tick, 0, sizeof(s_recovery_tick));
    memset((void *)s_temp_dev_cnt, 0, sizeof(s_temp_dev_cnt));
    memset((void *)s_hard_implausible_cnt, 0, sizeof(s_hard_implausible_cnt));
    memset((void *)s_soft_retry_cnt, 0, sizeof(s_soft_retry_cnt));
    memset((void *)s_declared_faulty, 0, sizeof(s_declared_faulty));
    memset((void *)s_lockup_pending, 0, sizeof(s_lockup_pending));
    s_last_push_tick = HAL_GetTick();

    /* AN5590 Section 6: after VDD is applied, the sensor needs up to 4.5ms
     * (max) to load its trimming parameters; registers are not accessible
     * until then. This is the first place in the boot sequence that touches
     * the sensor over I2C, so guarantee the margin here regardless of how
     * much wall-clock time earlier init steps (clock config, other
     * peripheral inits) happened to already consume. */
    HAL_Delay(5U);

    for (uint8_t ch = 0U; ch < SENSOR_CH_NUM; ch++) {
        if (sensor_probe_and_config(ch) == HAL_OK) {
            g_sensor[ch].present    = 1U;
            g_sensor[ch].data_valid = 0U;
            g_sensor[ch].use_fake   = 0U;
            g_sensor[ch].warmup     = 1U;
            g_sensor[ch].state      = SS_IDLE;
        } else {
            (void)HAL_I2C_DeInit(sensor_i2c(ch));
            i2c_bus_recover(ch);
            sensor_enter_recovery(ch, HAL_GetTick());
            g_sensor[ch].err_cnt++;
        }
    }
    sensor_service_build_uart_frame();
}

void sensor_service_i2c_poll(void)
{
    uint32_t now = HAL_GetTick();

    for (uint8_t ch = 0U; ch < SENSOR_CH_NUM; ch++) {
        sensor_ch_t *s = &g_sensor[ch];

        if (s_declared_faulty[ch] != 0U) {
            /* Parked: soft-reset retries exhausted on a channel with no
             * power-cycle pin. Only a manual SWRESET/BOOT (or a future
             * power-cycle wiring) clears this - never silently re-trusted. */
            continue;
        }

        if (s_lockup_pending[ch] != 0U) {
            s_lockup_pending[ch] = 0U;
            if (s_soft_retry_cnt[ch] < SENSOR_LOCKUP_SOFT_RETRY_LIMIT) {
                /* Field-validated recovery sequence: lower ODR, BOOT, SWRESET,
                 * restore ODR. Order follows AN5590 Section 6's mandated flow
                 * (BOOT first, then SWRESET - never both bits set at once).
                 * Confirmed effective for a channel resolving its normal I2C
                 * address (0x5C) - BOOT/SWRESET alone at the existing (200Hz)
                 * ODR did NOT clear the lock-up in testing, only this full
                 * sequence did. On a channel stuck resolving the *alternate*
                 * address (0x5D - see lps27hh_alt_addr()), this sequence
                 * reconfigures the registers but does NOT reliably clear the
                 * underlying analog fault; that case tracks with a physical
                 * SA0/GND contact issue rather than a plain power-on lock-up,
                 * and isn't something a register sequence can fix from
                 * software. Each step below is independently claim-guarded/
                 * blocking; fine to call back-to-back here. */
                s_soft_retry_cnt[ch]++;
                term_printf(TPID_DEBUG,
                            "[I2C%u] addr=0x%02X implausible reading detected (p=%uhPa raw=%ld, temp=%d) - recovery attempt %lu/%lu\r\n",
                            (unsigned int)(ch + 1U), (unsigned int)sensor_service_i2c_get_addr(ch),
                            (unsigned int)s->value_hpa, (long)s->pressure_raw, (int)s->temp_raw,
                            (unsigned long)s_soft_retry_cnt[ch], (unsigned long)SENSOR_LOCKUP_SOFT_RETRY_LIMIT);

                HAL_StatusTypeDef r_odr_lo  = sensor_service_i2c_set_odr(ch, LPS27HH_ODR_CODE_10HZ); /* ODR=0 (power-down) write was found unreliable/flaky in testing - 10Hz confirmed to take effect */
                HAL_StatusTypeDef r_boot    = sensor_service_i2c_boot(ch);         /* AN5590: BOOT first */
                HAL_StatusTypeDef r_swreset = sensor_service_i2c_swreset(ch);      /* AN5590: then SWRESET; already reconfigures to 200Hz internally */
                HAL_StatusTypeDef r_odr_hi  = sensor_service_i2c_set_odr(ch, LPS27HH_ODR_CODE_200HZ); /* explicit, matches validated manual sequence */
                term_printf(TPID_DEBUG,
                            "[I2C%u] recovery seq result: odr10=%d boot=%d swreset=%d odr200=%d\r\n",
                            (unsigned int)(ch + 1U), (int)r_odr_lo, (int)r_boot, (int)r_swreset, (int)r_odr_hi);

                s->warmup        = 1U;
                s->state         = SS_IDLE;
                s_temp_dev_cnt[ch]         = 0U;
                s_hard_implausible_cnt[ch] = 0U;
            } else {
                /* Exhausted soft-reset attempts with no hardware fix
                 * available - park it as untrusted rather than keep handing
                 * out a frozen bad reading as if it were valid data. */
                term_printf(TPID_DEBUG,
                            "[I2C%u] addr=0x%02X recovery exhausted after %lu attempts - parking channel as faulty (present forced 0)\r\n",
                            (unsigned int)(ch + 1U), (unsigned int)sensor_service_i2c_get_addr(ch),
                            (unsigned long)SENSOR_LOCKUP_SOFT_RETRY_LIMIT);
                sensor_clear_data(ch);
                s->present           = 0U;
                s_declared_faulty[ch] = 1U;
            }
            continue; /* re-evaluate this channel fresh next poll. */
        }

        if ((s->present == 0U) && (s->state != SS_RECOVERY)) { continue; }

        if (s->state == SS_RECOVERY) {
            if ((now - s_recovery_tick[ch]) >= I2C_RECOVERY_BACKOFF_MS) {
                I2C_HandleTypeDef *hi2c = sensor_i2c(ch);
                (void)HAL_I2C_DeInit(hi2c);
                i2c_bus_recover(ch);
                if ((HAL_I2C_Init(hi2c) == HAL_OK) &&
                    (sensor_probe_and_config(ch) == HAL_OK)) {
                    s->present    = 1U;
                    s->data_valid = 0U;
                    s->use_fake   = 0U;
                    s->warmup     = 1U;
                    s->state      = SS_IDLE;
                    sensor_err_reset(ch);
                } else {
                    sensor_enter_recovery(ch, now);
                }
            }
            continue;
        }

        if (s->use_fake != 0U) {
            if ((now - s->last_tick) >= SENSOR_POLL_MS) {
                s->last_tick = now;
                sensor_fake_update(ch);
            }
            continue;
        }

        if (s->state == SS_BUSY) {
            if ((now - s->busy_start_tick) >= I2C_BUSY_TIMEOUT_MS) {
                /* Neither MemRxCplt nor Error callback ever fired - the
                 * transfer is not just failed, it is stuck. Reset hard
                 * immediately rather than waiting out I2C_CONSEC_ERR_LIMIT,
                 * which would never be reached since nothing is
                 * incrementing it. */
                s->err_cnt++;
                s->last_i2c_err = HAL_I2C_GetError(sensor_i2c(ch));
                sensor_force_reset_and_recover(ch, now);
            }
            continue;
        }

        if (sensor_err_get(ch) >= I2C_CONSEC_ERR_LIMIT) {
            sensor_enter_recovery(ch, now);
            continue;
        }

        if ((now - s->last_tick) < SENSOR_POLL_MS) { continue; }

        I2C_HandleTypeDef *hi2c = sensor_i2c(ch);
        s->last_tick = now;
        s->state = SS_BUSY;
        s->busy_start_tick = now;
        s->start_cnt++;

        HAL_StatusTypeDef ret = HAL_I2C_Mem_Read_DMA(hi2c,
                                                     LPS27HH_ADDR_SHIFTED(sensor_addr(ch)),
                                                     LPS27HH_PRESS_OUT_XL, I2C_MEMADD_SIZE_8BIT,
                                                     s->rx_raw, 5U);
        if (ret != HAL_OK) {
            sensor_clear_data(ch);
            s->err_cnt++;
            s->last_i2c_err = HAL_I2C_GetError(hi2c);
            sensor_err_inc(ch);
            if (sensor_err_get(ch) >= I2C_CONSEC_ERR_LIMIT) {
                sensor_enter_recovery(ch, now);
            } else {
                s->state = SS_IDLE;
            }
        }
    }

#if SENSOR_TRANSPORT_USE_UART
    if ((now - s_last_push_tick) < SENSOR_UART_PUSH_PERIOD_MS) { return; }
    s_last_push_tick = now;

    if (g_sensor_uart_busy != 0U) {
        g_sen1_drop_count++;
        return;
    }

    sensor_service_build_uart_frame();
    g_sensor_uart_busy = 1U;
    g_sen1_req_count++;
    if (HAL_UART_Transmit_DMA(&huart1, s_uart_tx_buf, SENSOR_UART_RESP_LEN) != HAL_OK) {
        g_sensor_uart_busy = 0U;
        g_sen1_drop_count++;
    }
#else
    (void)s_last_push_tick;
#endif
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    int8_t ch = sensor_ch_from_i2c(hi2c);
    if (ch < 0) { return; }

    sensor_ch_t *s = &g_sensor[(uint8_t)ch];

    if (s->warmup != 0U) {
        /* First sample right after (re)config: CTRL1 just enabled the ODR
         * and no STATUS/P_DA check is done here (would cost an extra
         * blocking transaction), so this sample may still reflect the
         * pre-conversion register state. Discard it once; the next one
         * (one SENSOR_POLL_MS period later, >= one ODR period away) is
         * guaranteed fresh. */
        s->warmup = 0U;
        s->state  = SS_IDLE;
        sensor_err_reset((uint8_t)ch);
        return;
    }

    uint32_t raw = ((uint32_t)s->rx_raw[2] << 16) |
                   ((uint32_t)s->rx_raw[1] << 8)  |
                   ((uint32_t)s->rx_raw[0]);

    s->pressure_raw = sign_extend24(raw);
    s->value_hpa    = (uint16_t)(s->pressure_raw / 4096);
    s->temp_raw     = (int16_t)(((uint16_t)s->rx_raw[4] << 8) | s->rx_raw[3]);
    s->temp_c_x100  = s->temp_raw;
    s->present      = 1U;
    s->data_valid   = 1U;
    s->sample_cnt++;
    s->state        = SS_IDLE;
    sensor_err_reset((uint8_t)ch);

    /* Hard absolute-implausibility check - short dwell, see defines above. */
    if ((s->temp_c_x100 > SENSOR_LOCKUP_TEMP_ABS_LIMIT_C100) ||
        (s->value_hpa < SENSOR_LOCKUP_HPA_MIN)) {
        if (s_hard_implausible_cnt[ch] < 0xFFFFFFFFU) { s_hard_implausible_cnt[ch]++; }
        if (s_hard_implausible_cnt[ch] >= SENSOR_LOCKUP_HARD_REPEAT_LIMIT) {
            s_lockup_pending[ch] = 1U;
        }
    } else {
        s_hard_implausible_cnt[ch] = 0U;
    }

    /* Cross-sensor plausibility check: compare this sample's temperature against
     * the average of the other channels currently considered trustworthy. */
    {
        int32_t sum = 0;
        uint8_t cnt = 0U;
        for (uint8_t peer = 0U; peer < SENSOR_CH_NUM; peer++) {
            if (peer == (uint8_t)ch) { continue; }
            if ((g_sensor[peer].present != 0U) && (g_sensor[peer].data_valid != 0U) &&
                (s_declared_faulty[peer] == 0U)) {
                sum += g_sensor[peer].temp_c_x100;
                cnt++;
            }
        }
        if (cnt > 0U) {
            int32_t avg = sum / (int32_t)cnt;
            int32_t dev = (int32_t)s->temp_c_x100 - avg;
            if (dev < 0) { dev = -dev; }
            if (dev >= SENSOR_LOCKUP_TEMP_DEV_LIMIT_C100) {
                if (s_temp_dev_cnt[ch] < 0xFFFFFFFFU) { s_temp_dev_cnt[ch]++; }
                if (s_temp_dev_cnt[ch] >= SENSOR_LOCKUP_TEMP_DEV_REPEAT_LIMIT) {
                    s_lockup_pending[ch] = 1U;
                }
            } else {
                s_temp_dev_cnt[ch] = 0U;
            }
        }
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    int8_t ch = sensor_ch_from_i2c(hi2c);
    if (ch < 0) { return; }

    sensor_ch_t *s = &g_sensor[(uint8_t)ch];
    sensor_clear_data((uint8_t)ch);
    s->err_cnt++;
    s->last_i2c_err = HAL_I2C_GetError(hi2c);
    sensor_err_inc((uint8_t)ch);
    if (sensor_err_get((uint8_t)ch) >= I2C_CONSEC_ERR_LIMIT) {
        sensor_enter_recovery((uint8_t)ch, HAL_GetTick());
    } else {
        s->state = SS_IDLE;
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
#if SENSOR_TRANSPORT_USE_DXL
        sensor_dxl_uart_tx_complete(huart);
#else
        g_sensor_uart_busy = 0U;
        g_sen1_rsp_count++;
#endif
    } else if (huart->Instance == USART2) {
        term_uart_tx_dma_cplt();
    }
}

volatile uint32_t g_uart1_err_count;
volatile uint32_t g_uart1_last_err;
volatile uint32_t g_uart2_err_count;
volatile uint32_t g_uart2_last_err;

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	uint32_t err = HAL_UART_GetError(huart);

	if (huart->Instance == USART1) {
        sensor_dxl_uart_error(huart);
        g_uart1_err_count++;
        g_uart1_last_err = err;

        #if !SENSOR_TRANSPORT_USE_DXL
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        g_sensor_uart_busy = 0U;
        g_sen1_drop_count++;
#endif
    } else if (huart->Instance == USART2) {
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        term_uart_tx_dma_error();
        g_uart2_err_count++;
        g_uart2_last_err = err;
    }
}

/* sensor_service_i2c_poll() runs independently every ~1ms and may already
 * have an async DMA read in flight on a channel's hi2c handle (state ==
 * SS_BUSY). Any foreground debug path (TEST/SCAN/SWRESET/BOOT) that issues
 * its own blocking HAL_I2C call on the same handle needs to avoid colliding
 * with that: HAL_I2C_Mem_Read/Write returns HAL_BUSY immediately without
 * touching the bus if it does, and HAL_I2C_IsDeviceReady() (used by SCAN)
 * would likewise report "not ready" for a device that is actually present
 * and fine - both look like real failures but aren't. sensor_channel_claim()
 * waits (bounded) for any in-flight transfer to finish, then marks the
 * channel busy so poll() leaves it alone; sensor_channel_release() restores
 * whatever state the channel was actually in (SS_RECOVERY included) rather
 * than blindly forcing SS_IDLE, so a channel mid-recovery isn't yanked out
 * of it by an unrelated SCAN/TEST. */
static sensor_state_t sensor_channel_claim(uint8_t ch)
{
    sensor_ch_t *s = &g_sensor[ch];
    uint32_t wait_start = HAL_GetTick();
    while ((s->state == SS_BUSY) && ((HAL_GetTick() - wait_start) < 20U)) {
        /* let the in-flight DMA transfer's ISR complete on its own. */
    }
    sensor_state_t prev = s->state;
    s->state           = SS_BUSY;
    s->busy_start_tick = HAL_GetTick(); /* keep the 50ms watchdog from tripping mid-sequence. */
    return prev;
}

static void sensor_channel_release(uint8_t ch, sensor_state_t prev)
{
    g_sensor[ch].state = prev;
}

HAL_StatusTypeDef sensor_service_i2c_test(uint8_t ch, uint8_t out[5])
{
    if (ch >= SENSOR_CH_NUM) { return HAL_ERROR; }

    sensor_state_t prev = sensor_channel_claim(ch);
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(sensor_i2c(ch),
                            LPS27HH_ADDR_SHIFTED(sensor_addr(ch)),
                            LPS27HH_PRESS_OUT_XL, I2C_MEMADD_SIZE_8BIT,
                            out, 5U, 100U);
    sensor_channel_release(ch, prev);
    return ret;
}

/* Debug-only: read the STATUS register (0x27h - P_DA/T_DA) directly. Mainly
 * useful to check whether the digital side (data-ready flags) still looks
 * normal while a channel is suspected of an analog front-end lock-up (e.g.
 * physically-impossible temperature/pressure repeating the exact same
 * value): if P_DA/T_DA keep toggling normally while the data itself is
 * garbage, that confirms the fault is analog, not digital/communication. */
HAL_StatusTypeDef sensor_service_i2c_read_status(uint8_t ch, uint8_t *status)
{
    if ((ch >= SENSOR_CH_NUM) || (status == NULL)) { return HAL_ERROR; }

    sensor_state_t prev = sensor_channel_claim(ch);
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(sensor_i2c(ch),
                            LPS27HH_ADDR_SHIFTED(sensor_addr(ch)),
                            LPS27HH_STATUS, I2C_MEMADD_SIZE_8BIT,
                            status, 1U, 20U);
    sensor_channel_release(ch, prev);
    return ret;
}

/* Debug-only: read any single register (e.g. CTRL_REG2 @0x11h) directly, to
 * confirm on real hardware what's actually configured right now - e.g.
 * verify IF_ADD_INC/LOW_NOISE_EN after a fix, or after SWRESET/BOOT. */
HAL_StatusTypeDef sensor_service_i2c_read_reg(uint8_t ch, uint8_t reg, uint8_t *out)
{
    if ((ch >= SENSOR_CH_NUM) || (out == NULL)) { return HAL_ERROR; }

    I2C_HandleTypeDef *hi2c = sensor_i2c(ch);
    sensor_state_t prev = sensor_channel_claim(ch);
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(hi2c,
                            LPS27HH_ADDR_SHIFTED(sensor_addr(ch)),
                            reg, I2C_MEMADD_SIZE_8BIT,
                            out, 1U, 20U);
    if (ret != HAL_OK) {
        g_sensor[ch].last_i2c_err = HAL_I2C_GetError(hi2c); /* surface the real cause via `I2C`/PDSTATUS-style status */
    }
    sensor_channel_release(ch, prev);
    return ret;
}

/* Debug-only: change ODR live without a reflash, for A/B testing whether ODR
 * affects analog lock-up frequency (per current investigation: scope shows
 * power rail isn't dirty, so testing ODR as the isolated variable instead).
 * Keeps BDU=1; does not touch CTRL2/LOW_NOISE_EN. Resets warmup and the
 * repeat-value lock-up tracking so the next samples are judged fresh at the
 * new rate rather than against a stale value from the old rate. */
HAL_StatusTypeDef sensor_service_i2c_set_odr(uint8_t ch, uint8_t odr_code)
{
    if ((ch >= SENSOR_CH_NUM) || (odr_code > 0x07U)) { return HAL_ERROR; }

    I2C_HandleTypeDef *hi2c = sensor_i2c(ch);
    sensor_state_t prev = sensor_channel_claim(ch);

    uint8_t ctrl1 = (uint8_t)((odr_code << 4) | 0x02U); /* ODR[6:4] | BDU(bit1)=1 */
    HAL_StatusTypeDef ret = HAL_ERROR;

    /* Confirmed in testing: an ODR write can complete with HAL_OK yet the
     * sensor silently ignores it (register readback still shows the old
     * value) - communication succeeds, the value just doesn't "take". So a
     * bare write can't be trusted; read CTRL_REG1 back and retry until it
     * actually matches, bounded so a genuinely dead channel doesn't hang. */
    for (uint8_t attempt = 0U; attempt < SENSOR_ODR_VERIFY_RETRY_LIMIT; attempt++) {
        ret = sensor_write_reg(ch, LPS27HH_CTRL_REG1, ctrl1);
        if (ret != HAL_OK) {
            g_sensor[ch].last_i2c_err = HAL_I2C_GetError(hi2c);
            break; /* real communication failure - retrying won't help, bail out */
        }

        uint8_t readback = 0U;
        HAL_StatusTypeDef rret = HAL_I2C_Mem_Read(hi2c,
                                LPS27HH_ADDR_SHIFTED(sensor_addr(ch)),
                                LPS27HH_CTRL_REG1, I2C_MEMADD_SIZE_8BIT,
                                &readback, 1U, 20U);
        if (rret != HAL_OK) {
            g_sensor[ch].last_i2c_err = HAL_I2C_GetError(hi2c);
            ret = rret;
            break; /* same - a real comms failure on the readback itself */
        }

        if (readback == ctrl1) {
            ret = HAL_OK; /* verified: register actually holds the new value */
            break;
        }

        /* Write "succeeded" but didn't stick - short settle then retry. */
        ret = HAL_ERROR;
        HAL_Delay(2U);
    }

    if (ret == HAL_OK) {
        g_sensor[ch].warmup = 1U;
    }

    sensor_channel_release(ch, prev);
    return ret;
}

uint8_t sensor_service_i2c_scan(uint8_t ch, uint8_t result[16])
{
    if (ch >= SENSOR_CH_NUM) { return 0U; }

    uint8_t found = 0U;
    I2C_HandleTypeDef *hi2c = sensor_i2c(ch);
    sensor_state_t prev = sensor_channel_claim(ch);
    for (uint8_t addr = 1U; addr <= 127U; addr++) {
        if (HAL_I2C_IsDeviceReady(hi2c, LPS27HH_ADDR_SHIFTED(addr), 2U, 10U) == HAL_OK) {
            if (found < 16U) { result[found] = addr; }
            found++;
        }
    }
    sensor_channel_release(ch, prev);

    if (found == 0U)        { result[0] = 0U; }
    else if (found < 16U)   { result[found] = 0U; }
    return found;
}

/* Shared sequence for CTRL_REG2 self-clearing "reset-style" bits (SWRESET,
 * BOOT): claim the channel from the periodic poller, write the bit, wait out
 * the device's internal reload/boot procedure, then re-run the normal
 * probe/config path (same one used at boot and after bus recovery) and
 * re-arm warmup so the first post-reset sample is discarded exactly like a
 * fresh boot. Blocking (HAL_Delay) is fine here - foreground debug command
 * handler only, never an ISR. */
static HAL_StatusTypeDef sensor_ctrl2_reset_bit(uint8_t ch, uint8_t bit, uint32_t settle_ms)
{
    if (ch >= SENSOR_CH_NUM) { return HAL_ERROR; }

    I2C_HandleTypeDef *hi2c = sensor_i2c(ch);
    sensor_ch_t *s = &g_sensor[ch];

    (void)sensor_channel_claim(ch); /* not restored on the way out: every path
                                      * below sets an explicit final state itself. */

    HAL_StatusTypeDef ret = sensor_write_reg(ch, LPS27HH_CTRL_REG2, bit);
    if (ret != HAL_OK) {
        s->last_i2c_err = HAL_I2C_GetError(hi2c);
        s->state        = SS_IDLE;
        return ret;
    }

    HAL_Delay(settle_ms); /* LPS27HHTW boot/reload procedure is up to 4.5ms max (registers
                           * inaccessible until then, per ST AN5590) - wait it out with margin
                           * before re-probing, or WHO_AM_I can read back stale/invalid data
                           * with no I2C error (ret=HAL_OK, wrong value) while still in progress. */

    if (sensor_probe_and_config(ch) == HAL_OK) {
        s->present    = 1U;
        s->data_valid = 0U;
        s->use_fake   = 0U;
        s->warmup     = 1U;
        s->state      = SS_IDLE;
        sensor_err_reset(ch);
        return HAL_OK;
    }

    sensor_enter_recovery(ch, HAL_GetTick());
    return HAL_ERROR;
}

/* Manual/debug-only: SWRESET restores all sensor registers (ODR, LOW_NOISE_EN,
 * etc.) to power-on defaults - use when the sensor's *configuration* is
 * suspected to be wrong/stuck. */
HAL_StatusTypeDef sensor_service_i2c_swreset(uint8_t ch)
{
    /* AN5590 Section 6: SWRESET completes in "a few tens of us" - much
     * shorter than BOOT's 4.5ms. 1ms is a safe practical minimum given
     * HAL_Delay()'s millisecond granularity (no need for a us-precision
     * delay primitive here). */
    return sensor_ctrl2_reset_bit(ch, LPS27HH_CTRL2_SWRESET, 1U);
}

/* Manual/debug-only: BOOT reloads the factory trimming/calibration
 * parameters from NVM (does not touch ODR/LOW_NOISE_EN/etc.) - use when the
 * sensor's *measurement/calibration* itself is suspected to be corrupted,
 * as a deeper alternative to SWRESET. */
HAL_StatusTypeDef sensor_service_i2c_boot(uint8_t ch)
{
    /* AN5590 Section 6: BOOT reload takes up to 4.5ms max (same as the
     * power-on boot procedure) - 6ms keeps the existing margin. */
    return sensor_ctrl2_reset_bit(ch, LPS27HH_CTRL2_BOOT, 6U);
}
