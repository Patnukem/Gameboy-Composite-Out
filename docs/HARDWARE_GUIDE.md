# Hardware Wiring Guide for DMG to Composite Converter

This document provides detailed wiring instructions for connecting a Game Boy DMG to the Raspberry Pi Pico composite video converter.

## Table of Contents

1. [Required Tools](#required-tools)
2. [Game Boy Signal Locations](#game-boy-signal-locations)
3. [Raspberry Pi Pico Connections](#raspberry-pi-pico-connections)
4. [R-2R DAC Construction](#r-2r-dac-construction)
5. [Complete Schematic](#complete-schematic)

---

## Required Tools

- Soldering iron (fine tip recommended)
- Solder (leaded or lead-free)
- Wire strippers
- Multimeter (for testing connections)
- Fine gauge wire (30 AWG recommended)
- Flux (optional but helpful)

---

## Game Boy Signal Locations

### DMG-CPU Board Signal Locations

The original Game Boy DMG uses a custom CPU that outputs video signals. These can be tapped from the following locations:

#### Option 1: CPU Pins (Recommended)

The CPU is the large chip on the main board. Pin numbering starts from the notch/dot.

| Signal | CPU Pin | Description |
|--------|---------|-------------|
| PIXEL_CLK | Pin 50 | ~4.19 MHz pixel clock |
| HSYNC | Pin 51 | Horizontal sync (active low) |
| VSYNC | Pin 52 | Vertical sync (active low) |
| DATA0 | Pin 48 | Pixel data bit 0 (LSB) |
| DATA1 | Pin 49 | Pixel data bit 1 (MSB) |
| GND | Multiple | Ground reference |
| VCC | Multiple | +5V (do not connect to Pico directly) |

#### Option 2: LCD Connector

The LCD ribbon cable connector also carries these signals. This may be easier to access.

```
LCD Connector Pinout (21 pins, from left to right looking at board):
┌─────────────────────────────────────────────────────────────┐
│  1   2   3   4   5   6   7   8   9  10  11  12  13  14 ... │
│ VCC GND  D1  D0 CLK  HS  VS                                 │
└─────────────────────────────────────────────────────────────┘

Pin 1: VCC (+5V)
Pin 2: GND
Pin 3: DATA1 (D1)
Pin 4: DATA0 (D0)  
Pin 5: PIXEL_CLK
Pin 6: HSYNC
Pin 7: VSYNC
```

### Visual Reference

```
    Game Boy DMG Main Board (Top View)
    ┌─────────────────────────────────────┐
    │                                     │
    │   ┌───────────────┐                 │
    │   │               │                 │
    │   │     CPU       │◄── Tap signals  │
    │   │   DMG-CPU     │    from here    │
    │   │               │                 │
    │   └───────────────┘                 │
    │                                     │
    │   ┌─────────────────────────┐       │
    │   │    LCD Connector        │       │
    │   │    ▼ ▼ ▼ ▼ ▼ ▼ ▼        │       │
    │   └─────────────────────────┘       │
    │                                     │
    │   ┌──────┐  ┌──────┐                │
    │   │ Cart │  │      │                │
    │   │ Slot │  │      │                │
    │   └──────┘  └──────┘                │
    │                                     │
    └─────────────────────────────────────┘
```

---

## Raspberry Pi Pico Connections

### Pico Pinout for This Project

```
                    Raspberry Pi Pico
                    ┌───────────────┐
           GP0  ────┤ 1          40 ├──── VBUS
           GP1  ────┤ 2          39 ├──── VSYS
           GND  ────┤ 3          38 ├──── GND
           GP2  ────┤ 4          37 ├──── 3V3_EN
           GP3  ────┤ 5          36 ├──── 3V3
           GP4  ────┤ 6          35 ├──── ADC_VREF
           GP5  ────┤ 7          34 ├──── GP28
           GND  ────┤ 8          33 ├──── GND
           GP6  ────┤ 9          32 ├──── GP27
           GP7  ────┤ 10         31 ├──── GP26
           GP8  ────┤ 11         30 ├──── RUN
           GP9  ────┤ 12         29 ├──── GP22 ◄── DATA1
           GND  ────┤ 13         28 ├──── GND
           GP10 ────┤ 14         27 ├──── GP21 ◄── DATA0
           GP11 ────┤ 15         26 ├──── GP20 ◄── PIXEL_CLK
           GP12 ────┤ 16         25 ├──── GP19 ◄── VSYNC
           GP13 ────┤ 17         24 ├──── GP18 ◄── HSYNC
           GND  ────┤ 18         23 ├──── GND
           GP14 ────┤ 19         22 ├──── GP17
           GP15 ────┤ 20         21 ├──── GP16
                    └───────────────┘

DAC Pins (Composite Output):
  GP0 - DAC Bit 0 (LSB)
  GP1 - DAC Bit 1
  GP2 - DAC Bit 2
  GP3 - DAC Bit 3
  GP4 - DAC Bit 4
  GP5 - DAC Bit 5 (MSB)

Game Boy Input Pins:
  GP18 - HSYNC
  GP27 - VSYNC
  GP26 - PIXEL_CLK
  GP20 - DATA0
  GP19 - DATA1

Note: Pin assignments match andy-west/consolized-game-boy VGA project
```

### Wiring Summary Table

| From | To | Wire Color (suggested) |
|------|-----|------------------------|
| GB HSYNC | Pico GP18 | Yellow |
| GB VSYNC | Pico GP27 | Orange |
| GB PIXEL_CLK | Pico GP26 | Green |
| GB DATA0 | Pico GP20 | Blue |
| GB DATA1 | Pico GP19 | Purple |
| GB GND | Pico GND | Black |

---

## R-2R DAC Construction

### Theory

An R-2R ladder DAC converts digital values to analog voltages. Each bit contributes to the output voltage based on its position. With 6 bits, we get 64 voltage levels (0-63).

Output Voltage = Vref × (Digital Value / 64)

At 3.3V reference, this gives us 0V to ~3.2V range, which we then attenuate for composite video levels.

### Component List

| Component | Quantity | Value | Notes |
|-----------|----------|-------|-------|
| R1-R6 | 6 | 1kΩ | 1% tolerance recommended |
| R7-R12 | 6 | 2kΩ | 1% tolerance recommended |
| R13 | 1 | 75Ω | Output impedance |
| C1 | 1 | 10µF | Electrolytic, 16V min |

### Schematic (ASCII)

```
                          1kΩ    1kΩ    1kΩ    1kΩ    1kΩ
                         ┌───┐  ┌───┐  ┌───┐  ┌───┐  ┌───┐
                    ┌────┤   ├──┤   ├──┤   ├──┤   ├──┤   ├──┐
                    │    └───┘  └─┬─┘  └─┬─┘  └─┬─┘  └─┬─┘  │
                    │             │      │      │      │    │
GP0 (LSB)──[2kΩ]───┤             │      │      │      │    │
                    │             │      │      │      │    │
GP1 ───────[2kΩ]───┼─────────────┤      │      │      │    │
                    │             │      │      │      │    │
GP2 ───────[2kΩ]───┼─────────────┼──────┤      │      │    │
                    │             │      │      │      │    │
GP3 ───────[2kΩ]───┼─────────────┼──────┼──────┤      │    │
                    │             │      │      │      │    │
GP4 ───────[2kΩ]───┼─────────────┼──────┼──────┼──────┤    │
                    │             │      │      │      │    │
GP5 (MSB)──[2kΩ]───┼─────────────┼──────┼──────┼──────┼────┤
                    │             │      │      │      │    │
                    └───[2kΩ]────┴──────┴──────┴──────┴────┼───┬───── Output
                                                           │   │      to RCA
                                                          10µF 75Ω
                                                           │   │
                                                          ─┴───┴───── GND
```

### Breadboard Layout

```
    ┌─────────────────────────────────────────────────────────────┐
    │  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●   │
    │                                                              │
    │  [2k] [2k] [2k] [2k] [2k] [2k]                              │
    │   │    │    │    │    │    │                                │
    │   │    │    │    │    │    └─[1k]─[1k]─[1k]─[1k]─[1k]─○ OUT │
    │   │    │    │    │    │              │                │     │
    │   │    │    │    │    └──────────────┘                │     │
    │   │    │    │    │                                  [75Ω]   │
    │   │    │    │    └───────────────────────────────────┤     │
    │   │    │    │                                        │     │
    │  GP0  GP1  GP2  GP3  GP4  GP5                      [10µF]  │
    │                                                      │     │
    │  ●──●──●──●──●──●──●──●──●──●──●──●──●──●──●──●──●──●──●   │
    │                         GND RAIL                          │
    └─────────────────────────────────────────────────────────────┘
```

### Alternative: Single Resistor Method

For testing, you can use a simpler single-resistor output (lower quality):

```
GP5 (MSB only) ────[330Ω]────┬──── Composite Out
                              │
                            [75Ω]
                              │
                            ─┴─── GND
```

This gives only 2 brightness levels but is useful for initial testing.

---

## Complete Schematic

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                                                                              │
│    GAME BOY DMG                         RASPBERRY PI PICO                   │
│    ┌──────────┐                        ┌─────────────────┐                  │
│    │          │                        │                 │                  │
│    │  HSYNC   ├────────────────────────┤ GP18            │                  │
│    │          │                        │                 │                  │
│    │  VSYNC   ├────────────────────────┤ GP27            │                  │
│    │          │                        │                 │                  │
│    │ PIXEL_CLK├────────────────────────┤ GP26            │                  │
│    │          │                        │                 │                  │
│    │  DATA0   ├────────────────────────┤ GP20            │                  │
│    │          │                        │                 │                  │
│    │  DATA1   ├────────────────────────┤ GP19            │                  │
│    │          │                        │                 │                  │
│    │   GND    ├────────────┬───────────┤ GND             │                  │
│    │          │            │           │                 │                  │
│    └──────────┘            │           │ GP0 ────[2kΩ]──┐                   │
│                            │           │ GP1 ────[2kΩ]──┤                   │
│                            │           │ GP2 ────[2kΩ]──┤                   │
│                            │           │ GP3 ────[2kΩ]──┼── R-2R ──┬──OUT   │
│                            │           │ GP4 ────[2kΩ]──┤   DAC    │        │
│                            │           │ GP5 ────[2kΩ]──┘         75Ω      │
│                            │           │                 │          │        │
│                            │           └─────────────────┘          │        │
│                            │                                        │        │
│                            └────────────────────────────────────────┘        │
│                                                                              │
│                                                                     ┌───┐   │
│                                          COMPOSITE OUTPUT ──────────┤RCA│   │
│                                          (Yellow Jack)              └───┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Testing Procedure

1. **Continuity Test**: Use multimeter to verify all connections
2. **Voltage Test**: With Game Boy powered, verify signal voltages (should be ~5V)
3. **Signal Test**: Connect oscilloscope if available to verify HSYNC/VSYNC timing
4. **Composite Test**: With DAC built, measure output voltage swing (should be 0-1V)
5. **Display Test**: Connect to TV and verify image appears

---

## Safety Notes

⚠️ **Important Safety Information**

1. The Game Boy operates at 5V logic. The Pico GPIO is 3.3V but tolerant of 5V input.
2. Do NOT connect the Game Boy's 5V power rail to the Pico's 3.3V pins.
3. Always double-check polarity before powering on.
4. Use a current-limiting resistor if unsure about connections.
5. Work in a static-free environment to protect the electronics.
