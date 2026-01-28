/*
 * DMG to Composite Converter
 * Based on andy-west/consolized-game-boy approach
 * 
 * GB capture: Bit-banged GPIO polling (no PIO)
 * Output: PWM-based composite
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

// Composite output
#define COMPOSITE_PIN   5

// GB input pins (matching andy-west pinout)
#define GB_HSYNC_PIN    18
#define GB_VSYNC_PIN    27
#define GB_CLK_PIN      26
#define GB_DATA0_PIN    20
#define GB_DATA1_PIN    19

#define LED_PIN         25

// GB display dimensions
#define GB_WIDTH        160
#define GB_HEIGHT       144

// Double framebuffer for robust decoupling
static uint8_t gb_framebuffer[2][GB_HEIGHT][GB_WIDTH];
static volatile int write_buffer = 0;
static volatile int read_buffer = 0;
static volatile bool frame_ready = false;
static volatile bool boot_complete = false;

// PWM slice
static uint pwm_slice;

// Composite levels (6-bit PWM, 0-63)
#define SYNC_LEVEL      0
#define BLANK_LEVEL     17   // ~27% = 0.3V (blanking)
#define BLACK_LEVEL     17   // Same as blank
#define WHITE_LEVEL     63   // 100% = 1V (white)

// Grayscale lookup: GB pixel (0-3) -> PWM level
// GB: 0=lightest, 3=darkest
// Evenly spaced from pure black to pure white for better contrast
static const uint8_t gray_lut[4] = {
    63,  // 0 = white (lightest)
    42,  // 1 = light gray (evenly spaced)
    21,  // 2 = dark gray (evenly spaced)
    0    // 3 = black (darkest)
};

// 8x8 font for sync stabilization frame (not displayed)
// Simple blocky font - kept for reference but not used
static const uint8_t font_B[] = {0xFC,0xC6,0xC6,0xFC,0xC6,0xC6,0xFC,0x00};
static const uint8_t font_U[] = {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00};
static const uint8_t font_F[] = {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0,0x00};
static const uint8_t font_E[] = {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xFE,0x00};
static const uint8_t font_R[] = {0xFC,0xC6,0xC6,0xFC,0xD8,0xCC,0xC6,0x00};

static const uint8_t* logo_chars[] = {font_B, font_U, font_F, font_F, font_E, font_R};
#define LOGO_LEN 6
#define CHAR_W 8
#define CHAR_H 8
#define LOGO_SCALE 2  // 2x scale = 16x16 per char
#define LOGO_W (LOGO_LEN * CHAR_W * LOGO_SCALE)  // 112 pixels wide
#define LOGO_H (CHAR_H * LOGO_SCALE)  // 16 pixels tall

static inline void set_level(uint8_t level) {
    pwm_set_gpio_level(COMPOSITE_PIN, level);
}

// Draw the CRTendo logo into framebuffer at given Y position
static void draw_logo(uint8_t (*buf)[GB_WIDTH], int logo_y) {
    // Clear buffer to white (GB color 0)
    for (int y = 0; y < GB_HEIGHT; y++) {
        for (int x = 0; x < GB_WIDTH; x++) {
            buf[y][x] = 0;  // White background
        }
    }
    
    // Draw border (like GB boot) - for sync stabilization only
    for (int x = 0; x < GB_WIDTH; x++) {
        buf[0][x] = 3;  // Top border
        buf[GB_HEIGHT-1][x] = 3;  // Bottom border
    }
    for (int y = 0; y < GB_HEIGHT; y++) {
        buf[y][0] = 3;  // Left border
        buf[y][GB_WIDTH-1] = 3;  // Right border
    }
    
    // Text drawing removed - just border for sync stabilization
}

// NOP delay for timing adjustment after clock edge
static inline void pixel_delay(void) {
    asm volatile(
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
    );
}

/*
 * Core 0: Capture GB video
 * Continuously capture frames as they come, using double buffering
 */
void capture_gb_video(void) {
    // Wait for minimal boot sync stabilization
    while (!boot_complete) {
        tight_loop_contents();
    }

    printf("Starting continuous capture...\n");

    while (true) {
        // Wait for VSYNC falling edge (active low)
        while (gpio_get(GB_VSYNC_PIN) == 1) tight_loop_contents();
        while (gpio_get(GB_VSYNC_PIN) == 0) tight_loop_contents();

        int current_write = write_buffer;
        // Capture 144 lines to write buffer
        for (int y = 0; y < GB_HEIGHT; y++) {
            while (gpio_get(GB_HSYNC_PIN) == 0) tight_loop_contents();
            while (gpio_get(GB_HSYNC_PIN) == 1) tight_loop_contents();
            for (int x = 0; x < GB_WIDTH; x++) {
                while (gpio_get(GB_CLK_PIN) == 1) tight_loop_contents();
                while (gpio_get(GB_CLK_PIN) == 0) tight_loop_contents();
                gb_framebuffer[current_write][y][x] = (gpio_get(GB_DATA0_PIN) << 1) | gpio_get(GB_DATA1_PIN);
            }
        }
        // Swap buffers atomically
        read_buffer = current_write;
        write_buffer = 1 - current_write;
        frame_ready = true;
    }
}

/*
 * Core 1: Output composite video
 * Live line-by-line capture synchronized to GB HSYNC
 */
void output_composite(void) {
    // PAL/CCIR timing (European standard) - optimized for CCIR monitors
    // NTSC timing commented out for reference:
    // const int LINE_US = 63;
    // const int HSYNC_US = 4;
    // const int BACK_PORCH_US = 5;
    // const int ACTIVE_VIDEO_US = 54;
    // const int TOTAL_LINES = 263;
    // const int VSYNC_LINES = 3;
    // const int PRE_LINES = 24;
    // const int IMAGE_LINES = 216;
    
    const int LINE_US = 64;  // PAL line time (~64µs)
    const int HSYNC_US = 4;
    const int BACK_PORCH_US = 5;  // Back to original
    const int ACTIVE_VIDEO_US = 55;  // Back to original
    const int TOTAL_LINES = 312;  // Back to original
    const int VSYNC_LINES = 3;   // PAL VSYNC
    const int PRE_LINES = 40;    // Back to original
    const int IMAGE_LINES = 216; // Keep GB scaled output
    const int POST_LINES = TOTAL_LINES - VSYNC_LINES - PRE_LINES - IMAGE_LINES;

    int frame_count = 0;
    bool led_state = false;

    // Minimal boot sync stabilization - just 1 frame of border
    int boot_frame = 0;
    bool boot_done = false;
    uint8_t logo_buf[GB_HEIGHT][GB_WIDTH];

    while (true) {
        // Minimal sync stabilization: 1 frame of border
        if (!boot_done) {
            draw_logo(logo_buf, 0);  // Draw border at top
            boot_frame++;
            if (boot_frame >= 1) {
                boot_done = true;
                boot_complete = true;
            }
        }

        // VSYNC - broad sync pulses
        for (int i = 0; i < VSYNC_LINES; i++) {
            set_level(SYNC_LEVEL);
            busy_wait_us_32(LINE_US - HSYNC_US);
            set_level(BLANK_LEVEL);
            busy_wait_us_32(HSYNC_US);
        }

        // Pre-image blank lines
        for (int i = 0; i < PRE_LINES; i++) {
            set_level(SYNC_LEVEL);
            busy_wait_us_32(HSYNC_US);
            set_level(BLANK_LEVEL);
            busy_wait_us_32(LINE_US - HSYNC_US);
        }

        // Active video lines
        for (int line = 0; line < IMAGE_LINES; line++) {
            int gb_y = (line * GB_HEIGHT) / IMAGE_LINES;
            if (gb_y >= GB_HEIGHT) gb_y = GB_HEIGHT - 1;

            set_level(SYNC_LEVEL);
            busy_wait_us_32(HSYNC_US);
            set_level(BLANK_LEVEL);
            busy_wait_us_32(BACK_PORCH_US);

            if (!boot_done) {
                uint8_t pixel = logo_buf[gb_y][line % GB_WIDTH] & 0x03;
                set_level(gray_lut[pixel]);
                busy_wait_us_32(ACTIVE_VIDEO_US);
            } else {
                int current_read = read_buffer;
                if (frame_ready) {
                    for (int x = 0; x < GB_WIDTH; x++) {
                        uint8_t pixel = gb_framebuffer[current_read][gb_y][x] & 0x03;
                        set_level(gray_lut[pixel]);
                        asm volatile(
                            "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                            "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                            "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                            "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                            "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                        );
                    }
                } else {
                    set_level(40);
                    busy_wait_us_32(ACTIVE_VIDEO_US);
                }
            }

            set_level(BLANK_LEVEL);
            busy_wait_us_32(0);
        }

        // Post-image blank lines
        for (int i = 0; i < POST_LINES; i++) {
            set_level(SYNC_LEVEL);
            busy_wait_us_32(HSYNC_US);
            set_level(BLANK_LEVEL);
            busy_wait_us_32(LINE_US - HSYNC_US);
        }

        // Blink LED every ~30 frames
        frame_count++;
        if (frame_count >= 30) {
            frame_count = 0;
            led_state = !led_state;
            gpio_put(LED_PIN, led_state);
        }
    }
}

// Core 1 entry point
void core1_entry(void) {
    output_composite();
}

int main(void) {
    // Moderate overclock to 200MHz for stable timing (balance between speed and stability)
    set_sys_clock_khz(200000, true);
    
    // Re-init stdio after clock change
    stdio_init_all();
    
    // LED setup
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
    
    // GB input pins setup
    gpio_init(GB_HSYNC_PIN);
    gpio_init(GB_VSYNC_PIN);
    gpio_init(GB_CLK_PIN);
    gpio_init(GB_DATA0_PIN);
    gpio_init(GB_DATA1_PIN);
    gpio_set_dir(GB_HSYNC_PIN, GPIO_IN);
    gpio_set_dir(GB_VSYNC_PIN, GPIO_IN);
    gpio_set_dir(GB_CLK_PIN, GPIO_IN);
    gpio_set_dir(GB_DATA0_PIN, GPIO_IN);
    gpio_set_dir(GB_DATA1_PIN, GPIO_IN);
    
    // PWM setup for composite output
    gpio_set_function(COMPOSITE_PIN, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(COMPOSITE_PIN);
    // 250MHz / 64 = ~3.9MHz PWM frequency
    pwm_set_wrap(pwm_slice, 63);
    pwm_set_clkdiv(pwm_slice, 1.0f);
    pwm_set_enabled(pwm_slice, true);
    
    printf("DMG Composite Converter\n");
    printf("Clock: %d MHz\n", clock_get_hz(clk_sys) / 1000000);
    printf("Starting video output on core1...\n");
    
    // Start composite output on core 1
    multicore_launch_core1(core1_entry);
    
    sleep_ms(100);  // Let output stabilize
    
    printf("Starting GB capture on core0...\n");
    
    // Run GB capture on core 0 (main)
    capture_gb_video();
    
    return 0;
}
