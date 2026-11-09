# 개요

| 항목 | 내용 |
| --- | --- |
| 프로젝트 | 02_V6S_Sensor — 양산 보드용 V6S 압력 센서 노드 |
| MCU | STM32G431KBUx (UFQFPN32, 160 MHz, HSI16 PLL) |
| 베이스 | 95_V5S2_Sensor_Nucleo_G431 (G431KBT, LQFP32) 의 양산 패키지 포팅본 |
| 역할 | LPS27HHTWTR 압력센서 3채널 수집 및 Main board(01)로 센서값 전송 |
| I2C 채널 | I2C1 (CH0, addr 0x5C), I2C2 (CH1, addr 0x5C), I2C3 (CH2, addr 0x5C) |
| 센서 UART | USART1 (Main board uplink, 2 Mbaud, DMA) |
| 센서 CAN | FDCAN1 (Main board uplink, FD+BRS, 1 Mbps / 4 Mbps) |
| 디버그 UART | USART2 (921600 baud, VCP console, NVIC IT + DMA TX, non-blocking) |
| LED | WS2812B 1개 (TIM2_CH1 PWM DMA, DMA_MEMORY_TO_PERIPH) |
| DMA | I2C1/2/3 TX/RX + USART1 TX/RX + USART2 TX/RX + TIM2_CH1 |
| 워치독 | IWDG (LSI 32 kHz, prescaler 64, reload 2500 → 약 5 s 타임아웃) |
| 패키지 변경점 | LQFP32 → UFQFPN32. 핀 기능 매핑은 동일하나 PCB 배선 시 PF0(I2C2_SDA) 확보 필요 |

# 95 대비 변경 사항

| 항목 | 95 (G431KBT) | 02 (G431KBU) |
| --- | --- | --- |
| 패키지 | LQFP32 | UFQFPN32 |
| Debug UART baud | 115200 (blocking polled) | **921600** (DMA TX 링버퍼) |
| USART2 NVIC | 미설정 | **활성 (priority 0/0)** |
| USART2 DMA TX | 없음 | **DMA2_Channel4** |
| USART2 DMA RX | 없음 | DMA2_Channel5 (term_print는 IT 사용 중) |
| DMA Request 수 | 9 | 11 |
| TIM2_CH1 DMA 방향 | DMA_MEMORY_TO_PERIPH | 동일 (포팅 초기 잘못 잡혔던 PERIPH_TO_MEMORY 수정됨) |
| I2C Timing | 0x40B285C2 | 동일 (포팅 초기 0x30D29DE4 → 95 검증값으로 통일) |

# 소스 파일 구성

| 파일 | 역할 |
| --- | --- |
| `Src/main.c` | HAL 초기화, V6S_Sense 서비스 초기화, 메인 루프 |
| `Src/stm32g4xx_hal_msp.c` | 주변장치 MSP — clock/GPIO/DMA/NVIC 설정 |
| `Src/stm32g4xx_it.c` | DMA/USART/SysTick 인터럽트 핸들러 |
| `V6S_Sense/Src/sensor_service_i2c.c` | LPS27HHTWTR I2C DMA 읽기 + UART1 DMA 프레임 전송 |
| `V6S_Sense/Src/can_sensor_tx.c` | g_sensor[] 값을 FDCAN1 24바이트 FD 프레임으로 전송 |
| `V6S_Sense/Src/monitor_service.c` | 주기 진단 출력 (MON 명령) |
| `V6S_Sense/Src/debug_feature.c` | CLI 명령 처리 (?, SENSOR, I2C, UART, MON, LED ...) |
| `V6S_Sense/Src/env_flash.c` | 파라미터 기본값 / working buffer 관리 |
| `V6S_Sense/Src/term_print.c` | 링 버퍼 기반 디버그 UART TX (DMA) / RX (IT) |
| `V6S_Sense/Src/ws2812.c` | TIM2_CH1 PWM DMA 기반 WS2812B LED 제어 |

# 주요 핀맵

| 주변장치 | 역할 | 핀 / 설정 |
| --- | --- | --- |
| I2C1 | CH0 Sensor Port | SCL PA15 / SDA PB7 |
| I2C2 | CH1 Sensor Port | SCL PA9 / SDA **PF0** (UFQFPN32에서도 PF0 외부 인출 필요) |
| I2C3 | CH2 Sensor Port | SCL PA8 / SDA PB5 |
| USART1 | Main board UART uplink | TX PB6 / RX PA10, 2 Mbaud, DMA |
| USART2 | Debug VCP console | TX PA2 / RX PA3, 921600 baud, NVIC + DMA |
| FDCAN1 | Main board CAN-FD uplink | RX PA11 / TX PA12 |
| TIM2_CH1 | WS2812B PWM | PA0 |
| GPIO | Test / LED | TP1 PA4, TP2 PA5, LED1 PB8 |
| SWD | Debug | SWDIO PA13 / SWCLK PA14 / SWO PB3 |

# DMA 채널 구성

| DMA | 채널 | 용도 | Mode |
| --- | --- | --- | --- |
| DMA1 | Channel 1 | I2C1 TX | NORMAL |
| DMA1 | Channel 2 | I2C1 RX | NORMAL |
| DMA1 | Channel 3 | I2C2 TX | NORMAL |
| DMA1 | Channel 4 | I2C2 RX | NORMAL |
| DMA1 | Channel 5 | I2C3 TX | NORMAL |
| DMA1 | Channel 6 | I2C3 RX | NORMAL |
| DMA2 | Channel 1 | USART1 TX (센서 프레임 푸시) | NORMAL |
| DMA2 | Channel 2 | USART1 RX | NORMAL |
| DMA2 | Channel 3 | TIM2_CH1 PWM waveform (WS2812) | NORMAL |
| DMA2 | Channel 4 | **USART2 TX** (디버그 콘솔 DMA) | NORMAL |
| DMA2 | Channel 5 | USART2 RX (현재는 미사용, IT 경로) | NORMAL |

# 초기화 순서

```text
main()
  ↓
HAL_Init()
  ↓
SystemClock_Config()
  - HSI16 + PLLM/4 × PLLN/80 ÷ PLLR/2 = 160 MHz
  - APB1/APB2 = 160 MHz
  ↓
MX_GPIO_Init()
MX_DMA_Init()
MX_I2C1/2/3_Init()
MX_TIM2_Init()
MX_USART1_UART_Init()
MX_USART2_UART_Init()    ← 921600, NVIC + DMA TX 자동 연결
MX_FDCAN1_Init()
MX_IWDG_Init()
  ↓
init_terminal_buffers()
init_debug_history()
env_flash_init()
print_reset_info()
term_printf("[02] V6S G431 sensor firmware start")
term_printf("[02] UART1 sensor link=2Mbps DMA, UART2 debug VCP=921600")
start_uart_rx_interrupt()        ← USART2 RX IT 1바이트 arm
sensor_service_i2c_init()
[#if SENSOR_TRANSPORT_USE_CAN] can_sensor_tx_init()
ws2812_init() + ws2812_set_rgb(0, 16, 0)
```

# 메인 루프

```text
while (1)
  ↓
  sensor_service_i2c_poll()
    - 채널별 상태머신, I2C DMA read, USART1 DMA push
  ↓
  [#if SENSOR_TRANSPORT_USE_CAN] can_sensor_tx_service()
    - 1ms 주기 FDCAN1 FD+BRS 24바이트 push
  ↓
  g_loop_1ms_cnt++
  ↓
  uart_tx_service()
    - 링버퍼에 데이터가 있고 dma_tx_busy==0 이면 HAL_UART_Transmit_DMA(USART2)
  ↓
  debug_feature_service(TPID_DEBUG)
    - CLI 명령 처리
  ↓
  monitor_service(TPID_DEBUG)
    - 주기 진단 출력
  ↓
  HAL_IWDG_Refresh(&hiwdg)
```

# I2C 센서 수집 흐름

```text
sensor_service_i2c_init()
  ↓
각 채널별 WHO_AM_I 확인 (register 0x0F, expected 0xB3)
  ↓
LPS27HHTWTR 초기화
  - CTRL_REG2 = 0x12 (IF_ADD_INC=1, LOW_NOISE_EN=1)
  - CTRL_REG1 = 0x72 (ODR 200 Hz, BDU=1)
```

```text
sensor_service_i2c_poll()
  ↓
CH0 / CH1 / CH2 각각 상태머신
  ↓
[IDLE] elapsed >= SENSOR_POLL_MS(1ms) →
       HAL_I2C_Mem_Read_DMA(PRESS_OUT_XL, 5 bytes), state = BUSY
  ↓
[ISR] HAL_I2C_MemRxCpltCallback
       rx[0..2] → pressure_raw (signed 24-bit, 1000~1260 hPa 동작 범위)
       rx[3..4] → temp_raw    (signed 16-bit)
       value_hpa = pressure_raw / 4096
       sample_cnt++, state = IDLE
  ↓
[ISR] HAL_I2C_ErrorCallback
       err_cnt++, last_i2c_err 저장, state = ERROR → 다음 poll에서 재시도
```

# UART 전송 프로토콜 (USART1 → Main board)

| 항목 | 내용 |
| --- | --- |
| 기본 transport | SENSOR_TRANSPORT_UART |
| 주기 | 1 ms |
| 전송 | USART1 DMA, 2 Mbaud |
| 전체 길이 | 33 bytes = 3채널 × 11 bytes |
| busy 보호 | `g_sensor_uart_busy` |
| 완료 콜백 | `HAL_UART_TxCpltCallback()` → USART1 분기 |

**노드별 프레임 (11 bytes)**

| Byte | 내용 |
| --- | --- |
| [0] | 0xA5 헤더 |
| [1] | NODE_ID (1..3) |
| [2] | TYPE (0x02) |
| [3] | P_XL, pressure raw low |
| [4] | P_L, pressure raw mid |
| [5] | P_H, pressure raw high (24-bit signed 압력 raw — 1000~1260 hPa 표현 범위. 16-bit hPa로 압축 금지) |
| [6] | T_L, temperature raw low |
| [7] | T_H, temperature raw high |
| [8] | ERR, error/status |
| [9] | RSV, reserved |
| [10] | XOR checksum ([0]^[1]^...^[9]) |

# CAN-FD 전송 프로토콜 (FDCAN1)

| 항목 | 내용 |
| --- | --- |
| 사용 조건 | `SENSOR_TRANSPORT_CAN` 또는 `SENSOR_TRANSPORT_BOTH` |
| 주기 | 1 ms |
| 전송 | FDCAN1, FD+BRS, 1 Mbps nominal / 4 Mbps data |
| CAN ID | 0x100 + `CAN_SENSOR_THIS_PORT` |
| 기본 board ID | `CAN_SENSOR_THIS_PORT = 0` |
| 전체 길이 | 24 bytes = 3채널 × 7 bytes + 3 bytes padding |

**노드별 payload (7 bytes)**

| Byte | 내용 |
| --- | --- |
| [0] | node_id (1..3) |
| [1] | status (0=OK, 1=not present/error) |
| [2] | press_xl |
| [3] | press_l |
| [4] | press_h |
| [5] | temp_l |
| [6] | temp_h |

여러 G431 sensor board를 같은 CAN bus에 연결할 때 board별 `CAN_SENSOR_THIS_PORT` 값을 0..4 중 고유하게 설정 (Main board 측 매핑).

# Debug Console — USART2 921600 DMA

| 항목 | 내용 |
| --- | --- |
| 핀 | TX PA2 / RX PA3 |
| Baud | 921600 |
| TX 경로 | `term_printf` → 링버퍼 enqueue (non-blocking). `uart_tx_service`가 1 ms 슬롯에서 head/tail 청크를 `HAL_UART_Transmit_DMA`로 발사 |
| TX 완료 | `HAL_UART_TxCpltCallback` (USART2 분기) → `term_uart_tx_dma_cplt()` → tail 진행 + dma_tx_busy 클리어 |
| TX 에러 | `HAL_UART_ErrorCallback` (USART2 분기) → ORE/FE/NE 플래그 클리어 + `term_uart_tx_dma_error()` → RX IT 재무장 |
| RX 경로 | `HAL_UART_Receive_IT(huart2, 1 byte)` → `HAL_UART_RxCpltCallback` (USART2) → enqueue 후 재무장 |
| ISR 분기 | `sensor_service_i2c.c`의 `HAL_UART_TxCpltCallback`/`ErrorCallback`이 USART1/USART2 instance로 디스패치 |

# WS2812B LED

| 항목 | 내용 |
| --- | --- |
| 출력 핀 | PA0 (TIM2_CH1) |
| 방식 | TIM2 PWM + DMA (DMA_MEMORY_TO_PERIPH) |
| DMA 채널 | DMA2 Channel 3 |
| 기본 상태 | 부팅 후 `ws2812_set_rgb(0, 16, 0)` — 녹색 |
| CLI | `LED <r> <g> <b>` |

# 디버그 명령

| 명령 | 역할 |
| --- | --- |
| `?` | 도움말 출력 |
| `SENSOR` | 센서 상태 및 UART/CAN 요약 |
| `I2C TEST <0..2>` | 선택 채널 blocking pressure/temp read |
| `I2C SCAN <0..2>` | 선택 I2C bus scan |
| `UART` | UART1 stream counter 출력 |
| `MON ON` / `MON OFF` | monitor 출력 on/off |
| `MON INT <sec>` | monitor 주기 설정 |
| `MON MODE <mask>` | monitor 출력 mask 설정 (0x01=SYS, 0x02=SENSOR, 0x04=UART) |
| `MON ST` | monitor 상태 1회 출력 |
| `LED <r> <g> <b>` | WS2812B 색상 설정 |

# 파라미터

| 인덱스 | 이름 | 기본값 | 설명 |
| --- | --- | --- | --- |
| 0 | PARM_TRACE_FLAG | 0 | trace flag |
| 1 | PARM_MON_ONOFF | 0 | monitor on/off |
| 2 | PARM_MON_INTERVAL_SEC | 1 | monitor 출력 주기(초) |
| 3 | PARM_MON_FLAG | 0x07 | monitor 출력 bitmask |
| 4 | PARM_MON_LINE | 1 | monitor line mode |
| 5 | PARM_I2C_POLL_MS | 1 | I2C poll 주기용 파라미터 슬롯 |
| 6 | PARM_UART_DEBUG_ECHO | 1 | debug UART echo |

> 실제 I2C poll 주기는 `sensor_service_i2c.h`의 `SENSOR_POLL_MS(1ms)`를 우선 사용한다.

# Bring-up 체크리스트

1. **부팅 배너** — UART2 (921600 / 8N1)로 `[02] V6S G431 sensor firmware start` 출력 확인
2. **WS2812 LED** — 부팅 직후 녹색(0,16,0) 점등 확인. TIM2_CH1 DMA 방향 오류 시 점등 실패
3. **I2C SCAN** — 각 채널에서 0x5C 응답 확인. PF0 인출이 안 된 보드는 CH1 미응답
4. **UART1 frame** — 스코프 또는 Main board 수신 카운터로 1 ms 주기 33 byte burst 확인
5. **FDCAN1** — `SENSOR_TRANSPORT_CAN`으로 빌드 후 CAN analyzer로 ID 0x100+N, 24B FD frame 확인
6. **IWDG** — 메인 루프 정지 시 약 5 s 후 reset 발생 확인

# 관련 파일

| 파일 | 내용 |
| --- | --- |
| `02_V6S_Sensor_system_block.drawio` | 하드웨어 / 주변장치 / DMA / ISR / main loop 흐름도 |
| `02_V6S_Sensor_FWCube2.1.0.ioc` | CubeMX 설정 |
| `STM32G431KBUX_FLASH.ld` | 링커 스크립트 (Flash 128 KB @ 0x08000000, RAM 32 KB @ 0x20000000) |
| `Startup/startup_stm32g431kbux.s` | 리셋 핸들러 / 벡터 테이블 |
