
# DMG (Game Boy) to Composite Video Converter

---

**Note:**

- This is my first public release of this project. It was designed and tested specifically for use with a 4" CRT monitor.
- If you use this code or design in your own project, please consider giving attribution.
- The code and hardware are simple and may be easily improved or adapted for other displays or use cases—contributions and suggestions are welcome!

---

Convert your original Game Boy DMG video output to standard composite video using a Raspberry Pi Pico.

## Overview

This project captures the native video signals from a Game Boy DMG and converts them to composite video for display on any TV or monitor with composite input. The Raspberry Pi Pico's dual-core architecture enables robust, double-buffered, decoupled frame capture and composite output for stable CRT display.

The Game Boy's 4.194 MHz pixel clock requires sampling every ~238 nanoseconds. Only bare-metal programming with direct hardware access (like the Pico) can reliably achieve this.

## Game Boy DMG Video Specifications

| Parameter | Value |
|-----------|-------|
| Resolution | 160 × 144 pixels |
| Color Depth | 2-bit (4 shades) |
| Frame Rate | ~59.7 Hz |
| Pixel Clock | ~4.194 MHz |
| HSYNC | Active low |
| VSYNC | Active low |

The Game Boy outputs video data on two pins (DATA0 and DATA1) that together form a 2-bit value representing one of four grayscale levels.

## NTSC Composite Video Output

| Parameter | Value |
|-----------|-------|
| Resolution | 320 × 144 (2x horizontal scale) |
| Frame Rate | 60 Hz (non-interlaced) |
| Lines per Frame | 262 |
| Sync Level | 0V |
| Black Level | 0.3V (7.5 IRE) |
| White Level | 1.0V (100 IRE) |

## Hardware Requirements

### Components

| Component | Quantity | Notes |
|-----------|----------|-------|
| Raspberry Pi Pico | 1 | Standard or W version |
| Resistor 75Ω | 1 | Output impedance matching |
| Capacitor 10µF | 1 | DC blocking (electrolytic) |
| RCA Jack | 1 | Composite video output |
| Pin Headers | As needed | For connections |
| Perfboard | 1 | For DAC circuit |

## Wiring Diagram

### Game Boy DMG → Raspberry Pi Pico

Connect the following signals from the Game Boy's main board:

```
Game Boy DMG          Raspberry Pi Pico
─────────────         ─────────────────
HSYNC     ─────────── GPIO 18 (pin 24)
VSYNC     ─────────── GPIO 27 (pin 32)
PIXEL_CLK ─────────── GPIO 26 (pin 31)
DATA0     ─────────── GPIO 20 (pin 26)
DATA1     ─────────── GPIO 19 (pin 25)
GND       ─────────── GND (any ground)
```


**Note**: These pin assignments match the [andy-west VGA project](https://github.com/andy-west/consolized-game-boy), so you can use the same physical wiring if converting from that setup.


**Note**: Game Boy signals are active high, 5V logic. The Pico's GPIO is 3.3V but not technically 5V tolerant for input. Most people see normal results connecting 5V signals directly, but if you are concerned about long-term reliability or want extra protection, you can use a voltage divider or an 8-bit level shifter (as used in Andy West's project) to safely reduce the voltage to 3.3V. See his repo for an example: https://github.com/andy-west/consolized-game-boy

### Game Boy DMG Signal Locations

On the DMG main board (typically labeled DMG-CPU), locate these test points or pins:

| Signal | DMG-CPU Location |
|--------|------------------|
| HSYNC | Pin 51 of CPU     |
| VSYNC | Pin 52 of CPU     |
| PIXEL_CLK | Pin 50 of CPU |
| DATA0 | Pin 48 of CPU     |
| DATA1 | Pin 49 of CPU     |

Please visit [Andy's project for a better reference photo of these pins](https://github.com/andy-west/consolized-game-boy)
Alternatively, tap these signals from the LCD connector.




### PWM Composite Output (Tested)

The simplest and tested method uses high-speed PWM on GPIO 5:

**Circuit (Tested):**

- GPIO 5 → 330Ω resistor → RCA jack (center pin)
- RCA jack (center pin) → 75Ω resistor → GND
- RCA jack (shield) → GND

No capacitors are used in this setup. This works reliably for most CRTs and composite monitors. No external DAC is required—just the two resistors for proper voltage division and termination. This is the only version we have tested so far.

### R-2R DAC Output (Untested, Should Work)

For higher fidelity or compatibility with picky LCDs or capture cards, you can use a 6-bit R-2R resistor ladder DAC (see [andy-west/consolized-game-boy](https://github.com/andy-west/consolized-game-boy) for reference):

- GPIO 0–5 → R-2R ladder → 75Ω resistor → 10µF capacitor → RCA jack

This version is untested in this project, but should work since the output logic matches the reference design. Use this if you need cleaner voltage steps or want to experiment with other screens.

**Note:** The firmware is designed for PWM output, but can be adapted for R-2R DAC if needed. See the reference project for resistor values and wiring.

## Building the Firmware

### Prerequisites

1. Install the Pico SDK:
```bash
# macOS
brew install cmake arm-none-eabi-gcc

# Linux (Debian/Ubuntu)
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential

# Clone Pico SDK
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=$(pwd)
```

2. Clone this repository:
```bash
cd /path/to/your/projects
git clone <this-repo> dmg-composite
cd dmg-composite
```

### Build

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
make -j4
```

This produces `dmg_composite.uf2` in the build directory.

### Flash to Pico

1. Hold the BOOTSEL button on the Pico
2. Connect USB cable while holding BOOTSEL
3. Release BOOTSEL - Pico mounts as a USB drive
4. Copy `dmg_composite.uf2` to the Pico drive
5. Pico automatically reboots and starts running

## Usage

1. Wire up the Game Boy signals to the Pico as shown above
2. Build and flash the firmware
3. Connect the composite output to your TV
4. Power on the Game Boy
5. The display should appear on your TV!

### Troubleshooting

| Issue | Possible Cause | Solution |
|-------|---------------|----------|
| No picture | Wrong pin connections | Verify wiring, especially HSYNC/VSYNC |
| Rolling picture | VSYNC not detected | Check VSYNC connection and signal level |
| Garbled picture | Pixel clock issues | Verify PIXEL_CLK connection |
| Wrong colors/inverted | DATA pins swapped | Swap DATA0 and DATA1 |
| Picture too dark/bright | DAC calibration | Adjust resistor values |

## Technical Details


### Architecture

```
┌────────────────────────────────────────────────────────────┐
│                 Raspberry Pi Pico (Dual Core)              │
│                                                            │
│  ┌─────────────┐                  ┌─────────────┐          │
│  │   Core 0    │                  │   Core 1    │          │
│  │  GB Capture │◄──Double Buffer─►│ Composite   │          │
│  │             │                  │  Output     │          │
│  └─────────────┘                  └─────────────┘          │
│         │                            │                     │
│         │  GPIO                      │  PWM                │
│         ▼                            ▼                     │
│    Game Boy DMG                Composite Video Out         │
└────────────────────────────────────────────────────────────┘
```


### Timing Analysis

The Pico runs at 200–270 MHz, providing:
- **NTSC/CCIR line duration**: ~63 µs (263 lines per frame)
- **Double-buffered decoupling**: Ensures stable output with no jitter or waviness
- **PWM output**: 6-bit levels for grayscale, mapped to Game Boy pixel values

### Memory Usage

- **Framebuffer**: 2 × 160 × 144 = 46,080 bytes
- **Scanline buffers**: 2 × ~4,300 = ~8,600 bytes
- **Total RAM**: ~55 KB of 264 KB available

## License

MIT License - See LICENSE file for details.

## Credits

- Original VGA implementation reference: [andy-west/consolized-game-boy](https://github.com/andy-west/consolized-game-boy)
- Raspberry Pi Pico SDK: [raspberrypi/pico-sdk](https://github.com/raspberrypi/pico-sdk)


## Future Improvements

- [ ] Add color palette switching via button
- [ ] Add scanline filter option
- [ ] PAL/CCIR composite output option

---

## Attribution & Thanks

This project is based on the work of andy-west/consolized-game-boy and inspired by the open-source retro video community. Special thanks to everyone who contributed to Game Boy video reverse engineering and composite video generation on microcontrollers.

**Thank you for using and testing this project!**

If you found this useful, please consider sharing improvements or feedback.
