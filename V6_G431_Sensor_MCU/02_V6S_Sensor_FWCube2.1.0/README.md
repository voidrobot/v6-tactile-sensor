
# Allegro Hand V6 - G431 Sensor MCU 
Allegro Hand V6용 압력센서 (ETRI Sensor) MCU B'd F/W 관리를 위한 Git Repository

## Pin Assign

**Target B'd : AHSM_V6_UART_R2, AHSS_V6_XX**
```text
DEBUG
 ├─ SWD  : SWCLK, SWDIO, SWO, nRST (SWO NC)
 ├─ UART2 RX (PA2) : VCP UART RX
 ├─ UART2 TX (PA3) : VCP UART TX
 ├─ TP1 (PA4)
 └─ TP2 (PA5)

Communication I/F
 ├─ UART1 TX (PB6) : DXL_UART (Single Wire)
 ├─ USART DIR (PF1): DXL_UART_DIR (Level Shifter Dir)
 └─ LED (WS2812)   : PA0 (TIM2_CH1_PWM)

Sensor I/F
 ├─ I2C1 (Finger Tip)
 │   ├─ I2C1_SCL (PA15) 
 │   └─ I2C1_SDA (PB7)
 ├─ I2C2 (MCP)
 │   ├─ I2C2_SCL (PA9) 
 │   └─ I2C2_SDA (PF0)
 └─ I2C3 (PIP)
     ├─ I2C3_SCL (PA8) 
     └─ I2C3_SDA (PB5)

Not Used
 ├─ FDCAN1_TX : PA12
 └─ FDCAN1_RX : PA11
```

## Bootloader + application binary merge ##
1. **Post Build Setting**
 - IAP & Application PJT Build Setting
   - Project -> Properties -> Tool Setting -> -O binary, -O ihex
 - Application PJT Build Setting  
   - Build Step -> Post-build step 에 아래 내용 추가. (PJT 경로에 맞게 수정 할것.)

```text
  arm-none-eabi-objcopy -I ihex -O binary "D:/workplace/STM32CubeIDE/APH_V6/V6_G431_Sensor/V6_IAP_G431_Sensor_MCU/00_IAP_STM32G4_FWCube2.1.0/Release/00_IAP_STM32G4_FWCube2.1.0.hex" "bootloader_padded.bin" --pad-to=0x08004000 --gap-fill=0xff && cmd.exe /c "copy /b bootloader_padded.bin + ${BuildArtifactFileBaseName}.bin ${BuildArtifactFileBaseName}_merged.bin"
```
2. Download
 - CUBE Programmer를 이용한 다운로드. 
   - Erasing & Programming -> merger file select & Start Programming. 
   - 시작주소 : 0x08000000 (Bootloader 시작주소)  

 - Commandline Tool 이용한 다운로드 
 ```text
 STM32_Programmer_CLI.exe -c port=SWD -w "{파일경로\파일이름}" -v  
```
- CUBEIDE Program Download 설정
  - [Run Configurations] -> [Startup]
  - [Load Image and Symbols] -> Add -> File System -> (D:\workplace\..._merged.bin)를 선택
  - Download address (for .bin file) 입력 칸 -> 0x08000000 입력 (타겟 플래시 메모리 시작 주소)
  - Build, Load Symbol 은 기존 유지, 다운로드는 merge 파일 경로 선택
  - 다운로드가 마지막이 되도록 순서 조정
  - RUN 누르면, 빌드 후 다운로드 됨. 

