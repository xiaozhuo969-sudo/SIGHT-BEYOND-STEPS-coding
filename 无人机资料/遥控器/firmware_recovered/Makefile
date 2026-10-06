CROSS ?= arm-none-eabi-
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size
PYTHON ?= python3
CFLAGS := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -std=gnu11 -Os -g3 \
          -ffreestanding -ffunction-sections -fdata-sections -fno-builtin \
          -ffp-contract=off -Wall -Wextra -Werror -MMD -MP
LDFLAGS := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -nostdlib \
           -Tstm32f103c8.ld -Wl,--gc-sections,-Map=build/reconstructed.map
SOURCES := startup.c firmware_main.c remote_logic.c
OBJECTS := $(SOURCES:%.c=build/%.o)

.PHONY: all verify clean
all: build/reconstructed.elf build/reconstructed.bin build/reconstructed.hex

build:
	mkdir -p build

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/reconstructed.elf: $(OBJECTS) stm32f103c8.ld
	$(CC) $(LDFLAGS) $(OBJECTS) -lgcc -o $@
	$(SIZE) $@

build/reconstructed.bin: build/reconstructed.elf
	$(OBJCOPY) -O binary $< $@

build/reconstructed.hex: build/reconstructed.elf
	$(OBJCOPY) -O ihex $< $@

verify:
	$(PYTHON) verify_logic.py

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
