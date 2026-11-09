################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/common.c \
../Src/flash_if.c \
../Src/main.c \
../Src/menu.c \
../Src/stm32g4xx_hal_msp.c \
../Src/stm32g4xx_it.c \
../Src/syscalls.c \
../Src/sysmem.c \
../Src/system_stm32g4xx.c \
../Src/ymodem.c 

OBJS += \
./Src/common.o \
./Src/flash_if.o \
./Src/main.o \
./Src/menu.o \
./Src/stm32g4xx_hal_msp.o \
./Src/stm32g4xx_it.o \
./Src/syscalls.o \
./Src/sysmem.o \
./Src/system_stm32g4xx.o \
./Src/ymodem.o 

C_DEPS += \
./Src/common.d \
./Src/flash_if.d \
./Src/main.d \
./Src/menu.d \
./Src/stm32g4xx_hal_msp.d \
./Src/stm32g4xx_it.d \
./Src/syscalls.d \
./Src/sysmem.d \
./Src/system_stm32g4xx.d \
./Src/ymodem.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../Inc -I../Drivers/STM32G4xx_HAL_Driver/Inc -I../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src

clean-Src:
	-$(RM) ./Src/common.cyclo ./Src/common.d ./Src/common.o ./Src/common.su ./Src/flash_if.cyclo ./Src/flash_if.d ./Src/flash_if.o ./Src/flash_if.su ./Src/main.cyclo ./Src/main.d ./Src/main.o ./Src/main.su ./Src/menu.cyclo ./Src/menu.d ./Src/menu.o ./Src/menu.su ./Src/stm32g4xx_hal_msp.cyclo ./Src/stm32g4xx_hal_msp.d ./Src/stm32g4xx_hal_msp.o ./Src/stm32g4xx_hal_msp.su ./Src/stm32g4xx_it.cyclo ./Src/stm32g4xx_it.d ./Src/stm32g4xx_it.o ./Src/stm32g4xx_it.su ./Src/syscalls.cyclo ./Src/syscalls.d ./Src/syscalls.o ./Src/syscalls.su ./Src/sysmem.cyclo ./Src/sysmem.d ./Src/sysmem.o ./Src/sysmem.su ./Src/system_stm32g4xx.cyclo ./Src/system_stm32g4xx.d ./Src/system_stm32g4xx.o ./Src/system_stm32g4xx.su ./Src/ymodem.cyclo ./Src/ymodem.d ./Src/ymodem.o ./Src/ymodem.su

.PHONY: clean-Src

