# Simple Composite Output Alternative

If the 6-bit R-2R DAC seems like too much work, here are **simpler alternatives** to get you started:

## Option 1: Single Resistor (Test Only) ⚡

**Works for testing, black & white only**

```
GPIO 5 ────[330Ω]────┬──── RCA Center (Yellow)
                     │
                   [75Ω]
                     │
GND ─────────────────┴──── RCA Ground
```

Just 2 resistors! This will give you a basic black/white image to verify everything works.

## Option 2: 3-Bit DAC (Good Quality) ⭐ RECOMMENDED

**Much simpler, still looks great**

Only 6 resistors instead of 12:

```
GPIO 5 ──[2kΩ]──┬─[1kΩ]─┬─[1kΩ]─┬──┬── RCA Center
                │       │       │  │
GPIO 4 ──[2kΩ]──┤       │       │  │
                │       │       │  │
GPIO 3 ──[2kΩ]──┼───────┤       │ 75Ω
                │       │       │  │
                └───────┴───────┴──┴── GND
```

This gives you 8 shades instead of 64, which is still more than the Game Boy's 4 shades!

## Option 3: Buy a Breakout Board 🛒

Search for "R-2R DAC module" on Amazon/Adafruit. ~$5-10, plug and play.

## Which Should You Use?

- **Just testing?** → Option 1 (2 resistors)
- **Want good quality?** → Option 2 (6 resistors) 
- **Want perfection?** → Full 6-bit (12 resistors)
- **Don't want to solder?** → Buy a DAC module

The firmware is already built for the full 6-bit DAC, but it will work fine with any of these options!
