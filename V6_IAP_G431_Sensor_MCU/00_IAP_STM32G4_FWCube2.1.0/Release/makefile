################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include Startup/subdir.mk
-include Src/subdir.mk
-include Drivers/STM32G4xx_HAL_Driver/Src/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := 00_IAP_STM32G4_FWCube2.1.0
BUILD_ARTIFACT_EXTENSION := elf
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 
EXECUTABLES += \
00_IAP_STM32G4_FWCube2.1.0.elf \

MAP_FILES += \
00_IAP_STM32G4_FWCube2.1.0.map \

SIZE_OUTPUT += \
default.size.stdout \

OBJDUMP_LIST += \
00_IAP_STM32G4_FWCube2.1.0.list \

OBJCOPY_HEX += \
00_IAP_STM32G4_FWCube2.1.0.hex \

OBJCOPY_BIN += \
00_IAP_STM32G4_FWCube2.1.0.bin \


# All Target
all: main-build

# Main-build Target
main-build: 00_IAP_STM32G4_FWCube2.1.0.elf secondary-outputs

# Tool invocations
00_IAP_STM32G4_FWCube2.1.0.elf 00_IAP_STM32G4_FWCube2.1.0.map: $(OBJS) $(USER_OBJS) E:\workplace\STM32CubeIDE\APH_V6\V6_G431_Sensor\V6_IAP_G431_Sensor_MCU\00_IAP_STM32G4_FWCube2.1.0\STM32G431KBUX_FLASH.ld makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-gcc -o "00_IAP_STM32G4_FWCube2.1.0.elf" @"objects.list" $(USER_OBJS) $(LIBS) -mcpu=cortex-m4 -T"E:\workplace\STM32CubeIDE\APH_V6\V6_G431_Sensor\V6_IAP_G431_Sensor_MCU\00_IAP_STM32G4_FWCube2.1.0\STM32G431KBUX_FLASH.ld" --specs=nosys.specs -Wl,-Map="00_IAP_STM32G4_FWCube2.1.0.map" -Wl,--gc-sections -static --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -Wl,--start-group -lc -lm -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

default.size.stdout: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-size  $(EXECUTABLES)
	@echo 'Finished building: $@'
	@echo ' '

00_IAP_STM32G4_FWCube2.1.0.list: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objdump -h -S $(EXECUTABLES) > "00_IAP_STM32G4_FWCube2.1.0.list"
	@echo 'Finished building: $@'
	@echo ' '

00_IAP_STM32G4_FWCube2.1.0.hex: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O ihex $(EXECUTABLES) "00_IAP_STM32G4_FWCube2.1.0.hex"
	@echo 'Finished building: $@'
	@echo ' '

00_IAP_STM32G4_FWCube2.1.0.bin: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O binary $(EXECUTABLES) "00_IAP_STM32G4_FWCube2.1.0.bin"
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) 00_IAP_STM32G4_FWCube2.1.0.bin 00_IAP_STM32G4_FWCube2.1.0.elf 00_IAP_STM32G4_FWCube2.1.0.hex 00_IAP_STM32G4_FWCube2.1.0.list 00_IAP_STM32G4_FWCube2.1.0.map default.size.stdout
	-@echo ' '

secondary-outputs: $(SIZE_OUTPUT) $(OBJDUMP_LIST) $(OBJCOPY_HEX) $(OBJCOPY_BIN)

fail-specified-linker-script-missing:
	@echo 'Error: Cannot find the specified linker script. Check the linker settings in the build configuration.'
	@exit 2

warn-no-linker-script-specified:
	@echo 'Warning: No linker script specified. Check the linker settings in the build configuration.'

.PHONY: all clean dependents main-build fail-specified-linker-script-missing warn-no-linker-script-specified

-include ../makefile.targets
