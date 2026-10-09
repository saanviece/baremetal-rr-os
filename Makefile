# Build file for the bare-metal Round-Robin OS (ATmega328P, Arduino UNO).
# Usage:  make            build build/main.hex
#         make flash PORT=COM3
#         make test       run the simulator tests (needs simavr harness, see docs/M0_toolchain.md)
#         make disasm     human-readable assembly of the whole program
#         make clean
MCU    := atmega328p
F_CPU  := 16000000UL
BUILD  := build
PORT   ?= COM3

CC      := avr-gcc
OBJCOPY := avr-objcopy
OBJDUMP := avr-objdump
SIZE    := avr-size

# -Os: small code. -Wall -Wextra: show all warnings. -std=gnu11 gives me inline asm + modern C.
CFLAGS  := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=gnu11 -Wall -Wextra \
           -ffunction-sections -fdata-sections -Isrc $(EXTRA_CFLAGS)
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(basename $(notdir $@)).map

# Library modules in src/ (everything except main.c). The real program links all of them;
# an early-milestone test links only what it needs, listed in LIBS_<testname> below.
LIBS_DEFAULT := $(patsubst src/%.c,%,$(filter-out src/main.c,$(wildcard src/*.c)))
LIBS_m1_blink := -
LIBS_m2_uart := uart
LIBS_m3_tick := uart timer
libobjs = $(patsubst %,$(BUILD)/%.o,$(filter-out -,$(or $(LIBS_$(1)),$(LIBS_DEFAULT))))
vpath %.c src tests/fw
.SECONDEXPANSION:

.PHONY: all flash disasm test clean size
all: $(BUILD)/main.hex size

$(BUILD):
	mkdir $(BUILD)

$(BUILD)/%.o: %.c $(wildcard src/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# build/<name>.elf = <name>.o + its library modules. main uses src/main.c; any other
# name is a test firmware tests/fw/<name>.c
$(BUILD)/%.elf: $(BUILD)/%.o $$(call libobjs,$$*)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(BUILD)/main.elf
	$(SIZE) $<

disasm: $(BUILD)/main.elf
	$(OBJDUMP) -d -S $< > $(BUILD)/main.lst

flash: $(BUILD)/main.hex
	avrdude -c arduino -P $(PORT) -b 115200 -p m328p -U flash:w:$<

test:
	python tests/run_tests.py

clean:
	-rm -rf $(BUILD) 2>/dev/null || rmdir /s /q $(BUILD)

# Keep intermediate .o/.elf files
.SECONDARY:
