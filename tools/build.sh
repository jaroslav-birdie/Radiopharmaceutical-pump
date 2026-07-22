#!/bin/bash
# ============================================================
#  Kompilace pro Arduino Uno (ATmega328P) přes avr-g++.
#  Náhrada za arduino-cli v prostředích, kde nelze stáhnout
#  Arduino toolchain (offline/proxy). Vyžaduje balíčky:
#    gcc-avr avr-libc arduino-core-avr
#  Použití:  tools/build.sh          (z kořene projektu)
# ============================================================
set -e

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
CORE=/usr/share/arduino/hardware/arduino/avr/cores/arduino
VARIANT=/usr/share/arduino/hardware/arduino/avr/variants/standard
LIBS=/usr/share/arduino/hardware/arduino/avr/libraries
BUILD="$PROJ/build"
mkdir -p "$BUILD"

MCU="-mmcu=atmega328p"
DEFS="-DF_CPU=16000000L -DARDUINO=10806 -DARDUINO_AVR_UNO -DARDUINO_ARCH_AVR"
OPT="-Os -ffunction-sections -fdata-sections -flto"
CXXFLAGS="$MCU $DEFS $OPT -std=gnu++11 -fno-exceptions -fno-threadsafe-statics"
CFLAGS="$MCU $DEFS $OPT -std=gnu11"
INC="-I$CORE -I$VARIANT -I$LIBS/Wire/src -I$LIBS/Wire/src/utility -I$LIBS/EEPROM/src -I$PROJ"

# --- jádro Arduino (bez warningů – cizí kód) ---
CORE_OBJS=""
for f in "$CORE"/*.c "$CORE"/*.cpp "$LIBS"/Wire/src/Wire.cpp "$LIBS"/Wire/src/utility/twi.c; do
    base="$(basename "$f")"
    obj="$BUILD/core_${base%.*}.o"
    CORE_OBJS="$CORE_OBJS $obj"
    [ "$obj" -nt "$f" ] && continue
    case "$f" in
        *.c)   avr-gcc $CFLAGS -w $INC -c "$f" -o "$obj" ;;
        *.cpp) avr-g++ $CXXFLAGS -w $INC -c "$f" -o "$obj" ;;
    esac
done

# --- projektové soubory (přísné warningy) ---
PROJ_OBJS=""
for f in "$PROJ"/*.cpp; do
    base="$(basename "$f")"
    obj="$BUILD/${base%.*}.o"
    PROJ_OBJS="$PROJ_OBJS $obj"
    avr-g++ $CXXFLAGS -Wall -Wextra $INC -c "$f" -o "$obj"
done
# .ino se kompiluje jako C++
avr-g++ $CXXFLAGS -Wall -Wextra $INC -x c++ -c "$PROJ/Radiopharmaceutical-pump.ino" \
        -o "$BUILD/sketch.o"
PROJ_OBJS="$PROJ_OBJS $BUILD/sketch.o"

# --- link + statistiky ---
avr-g++ $MCU $OPT -Wl,--gc-sections -o "$BUILD/firmware.elf" $PROJ_OBJS $CORE_OBJS -lm
avr-objcopy -O ihex -R .eeprom "$BUILD/firmware.elf" "$BUILD/firmware.hex"

echo "=================================================="
avr-size "$BUILD/firmware.elf" | tail -1 | awk '{
    flash = $1 + $2; sram = $2 + $3;
    printf "Flash: %5d B / 32256 B  (%.1f %%)\n", flash, flash * 100 / 32256;
    printf "SRAM:  %5d B /  2048 B  (%.1f %%)\n", sram, sram * 100 / 2048;
}'
echo "=================================================="
