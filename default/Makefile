###############################################################################
# Makefile for NESert Golfing (Uzebox, video mode 6)
# Same layout as the kernel's demos: put this project folder in uzebox/demos/ so ../../../kernel resolves.
# Needs the opt-in M6_HIRES switch in the kernel's mode 6: see ../kernel_patch/README (apply once to the kernel).
###############################################################################

## General Flags
PROJECT = NESert
GAME = NESert
MCU = atmega644
TARGET = $(GAME).elf
CC = avr-gcc
INFO=../gameinfo.properties

## Kernel settings (same options as the kernel's VectorDemo, plus the RAM tile count this port is built for)
## RAM_TILES_COUNT must equal RAMTILES in ../src/nesert.h (checked at compile time by plat_uzebox.c)
KERNEL_DIR = ../../../kernel
## M6_HIRES=0 selects the 240px colour renderer (needs kernel_patch/ applied; the kernel default, M6_HIRES=1, is unchanged)
## The port reads the controllers itself (Hyperkin-safe timing, see plat_uzebox.c): kernel reading and mouse code off
KERNEL_OPTIONS = -DVIDEO_MODE=6 -DINTRO_LOGO=0 -DRAM_TILES_COUNT=128 -DSOUND_MIXER=1 -DM6_HIRES=0 -DCONTROLLERS_VSYNC_READ=0 -DSNES_MOUSE=0


## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -Wall -gdwarf-2 -std=gnu99 -DF_CPU=28636360UL -Os -fsigned-char -ffunction-sections -fno-toplevel-reorder
CFLAGS += -MD -MP -MT $(*F).o -MF dep/$(@F).d
CFLAGS += $(KERNEL_OPTIONS)

## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS += -Wl,-Map=$(GAME).map
LDFLAGS += -Wl,-gc-sections


## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings


## Objects that must be built in order to link
## (the mode 6 assembly is pulled in by uzeboxVideoEngineCore.s, as in the demos)
GAMEOBJECTS = plat_uzebox.o game.o render.o sfx.o
OBJECTS = uzeboxVideoEngineCore.o uzeboxCore.o uzeboxSoundEngine.o uzeboxSoundEngineCore.o uzeboxVideoEngine.o $(GAMEOBJECTS)

## Objects explicitly added by the user
LINKONLYOBJECTS =

## Include Directories
INCLUDES = -I"$(KERNEL_DIR)" -I"../src"

## Build
all: $(TARGET) $(GAME).hex $(GAME).eep $(GAME).lss $(GAME).uze size

## Compile Kernel files
uzeboxVideoEngineCore.o: $(KERNEL_DIR)/uzeboxVideoEngineCore.s
	$(CC) $(INCLUDES) $(ASMFLAGS) -c  $<

uzeboxSoundEngineCore.o: $(KERNEL_DIR)/uzeboxSoundEngineCore.s
	$(CC) $(INCLUDES) $(ASMFLAGS) -c  $<

uzeboxCore.o: $(KERNEL_DIR)/uzeboxCore.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

uzeboxSoundEngine.o: $(KERNEL_DIR)/uzeboxSoundEngine.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

uzeboxVideoEngine.o: $(KERNEL_DIR)/uzeboxVideoEngine.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<


## Compile game sources
plat_uzebox.o: ../src/plat_uzebox.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

game.o: ../src/game.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

render.o: ../src/render.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

sfx.o: ../src/sfx.c
	$(CC) $(INCLUDES) $(CFLAGS) -c  $<

##Link
## rebuild everything when this Makefile (e.g. KERNEL_OPTIONS) changes; make does not track flags by itself
$(OBJECTS): Makefile

$(TARGET): $(OBJECTS)
	 $(CC) $(LDFLAGS) $(OBJECTS) $(LINKONLYOBJECTS) $(LIBDIRS) $(LIBS) -o $(TARGET)

%.hex: $(TARGET)
	avr-objcopy -O ihex $(HEX_FLASH_FLAGS)  $< $@

%.eep: $(TARGET)
	-avr-objcopy $(HEX_EEPROM_FLAGS) -O ihex $< $@ || exit 0

%.lss: $(TARGET)
	avr-objdump -h -S $< > $@

%.uze: $(TARGET)
	-$(UZEBIN_DIR)packrom $(GAME).hex $@ $(INFO)

UNAME := $(shell sh -c 'uname -s 2>/dev/null || echo not')
AVRSIZEFLAGS := -A ${TARGET}
ifneq (,$(findstring MINGW,$(UNAME)))
AVRSIZEFLAGS := -C --mcu=${MCU} ${TARGET}
endif

size: ${TARGET}
	@echo
	@avr-size ${AVRSIZEFLAGS}

## Clean target
.PHONY: clean
clean:
	-rm -rf $(OBJECTS) $(GAME).* dep/* *.uze


## Other dependencies
-include $(shell mkdir dep 2>/dev/null) $(wildcard dep/*)
