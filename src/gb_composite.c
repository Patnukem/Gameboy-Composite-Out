/*
 * DMG (Game Boy) to Composite Video Converter - SIMPLE VERSION
 * 
 * Outputs composite video on a SINGLE GPIO pin using PWM for analog levels.
 * Much simpler approach - no PIO, no DMA, just direct GPIO toggling.
 * 
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

// ============================================================================
// Pin Definitions
// ============================================================================
// Game Boy Input (directly from DMG main board)
#define GB_HSYNC_PIN        18
#define GB_VSYNC_PIN        27
#define GB_PIXEL_CLOCK_PIN  26
#define GB_DATA_0_PIN       20
#define GB_DATA_1_PIN       19

// Composite Output - single pin with PWM for analog levels
#define COMPOSITE_PIN       5

// Status LED
#define LED_PIN             25

// ============================================================================
// Video Constants
// ============================================================================
#define GB_WIDTH            160
#define GB_HEIGHT           144

// CCIR/NTSC timing
#define LINE_TIME_US        63      // Line period
#define HSYNC_US            4       // Horizontal sync pulse
#define BACK_PORCH_US       4       // Back porch 
#define FRONT_PORCH_US      4       // Front porch
#define ACTIVE_US           (LINE_TIME_US - HSYNC_US - BACK_PORCH_US - FRONT_PORCH_US)

#define TOTAL_LINES         263     // Lines per frame
#define VSYNC_LINES         3       // Vertical sync lines
#define PRE_BLANK_LINES     18      // Lines before GB image
#define POST_BLANK_LINES    (TOTAL_LINES - VSYNC_LINES - PRE_BLANK_LINES - GB_HEIGHT)

// PWM levels (8-bit, will be scaled to 6-bit)
#define PWM_SYNC            0       // Sync tip
#define PWM_BLANK           70      // Blanking level
#define PWM_BLACK           70      // Black = blank
#define PWM_WHITE           255     // White (max)

// ============================================================================
// Framebuffers
// ============================================================================
static uint8_t framebuffer[2][GB_HEIGHT][GB_WIDTH];
static volatile uint8_t write_buffer = 0;
static volatile uint8_t read_buffer = 1;
static volatile bool frame_ready = false;
static volatile uint32_t frames_captured = 0;

// Sync between cores
static semaphore_t video_ready;

// Mode control - starts in test pattern, switches to GB when signal detected
static volatile bool test_pattern_mode = true;
static volatile bool gb_signal_detected = false;

// BOOTSEL button for mode switching (directly read via hardware register)
#define BOOTSEL_PIN  /* Read via special method */

// ============================================================================
// BOOTSEL button reading (directly from hardware)
// ============================================================================
#include "hardware/structs/ioqspi.h"
#include "hardware/sync.h"

static bool __no_inline_not_in_flash_func(get_bootsel_button)(void) {
    const uint CS_PIN_INDEX = 1;
    
    // Disable interrupts and flash access
    uint32_t flags = save_and_disable_interrupts();
    
    // Set chip select to input with pull-up
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                   GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                   IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    
    // Small delay
    for (volatile int i = 0; i < 1000; i++);
    
    // Read the button state (low = pressed)
    bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
    
    // Restore chip select
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                   GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                   IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    
    restore_interrupts(flags);
    
    return button_state;
}

// ============================================================================
// Set composite output level via PWM
// ============================================================================
static uint pwm_slice;

static inline void set_composite_level(uint8_t level) {
    // Scale 8-bit level to 6-bit (0-63) for faster PWM
    pwm_set_gpio_level(COMPOSITE_PIN, level >> 2);
}

// ============================================================================
// Core 1: Generate composite video output
// ============================================================================
void core1_video_output(void) {
    // Set up PWM on composite pin for analog output
    gpio_set_function(COMPOSITE_PIN, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(COMPOSITE_PIN);
    
    // Configure PWM: very high frequency for cleaner analog output
    // Use 64 levels instead of 256 for faster PWM: 125MHz / 64 = ~2MHz
    // This reduces fuzziness significantly
    pwm_set_wrap(pwm_slice, 63);
    pwm_set_clkdiv(pwm_slice, 1.0f);
    pwm_set_enabled(pwm_slice, true);
    
    // Signal ready
    sem_release(&video_ready);
    
    printf("Core 1: Starting video output...\n");
    
    while (true) {
        // === VSYNC (vertical sync) ===
        for (int line = 0; line < VSYNC_LINES; line++) {
            set_composite_level(PWM_SYNC);
            busy_wait_us_32(LINE_TIME_US - 4);
            set_composite_level(PWM_BLANK);
            busy_wait_us_32(4);
        }
        
        // === Pre-blank lines ===
        for (int line = 0; line < PRE_BLANK_LINES; line++) {
            set_composite_level(PWM_SYNC);
            busy_wait_us_32(HSYNC_US);
            set_composite_level(PWM_BLANK);
            busy_wait_us_32(LINE_TIME_US - HSYNC_US);
        }
        
        // === Active video lines ===
        for (int y = 0; y < GB_HEIGHT; y++) {
            // HSYNC
            set_composite_level(PWM_SYNC);
            busy_wait_us_32(HSYNC_US);
            
            // Back porch
            set_composite_level(PWM_BLANK);
            busy_wait_us_32(BACK_PORCH_US);
            
            if (test_pattern_mode) {
                // Test pattern: 8 bars
                for (int bar = 0; bar < 8; bar++) {
                    set_composite_level((bar & 1) ? PWM_BLACK : PWM_WHITE);
                    busy_wait_us_32(6);
                }
            } else {
                // Game Boy data
                uint8_t (*fb)[GB_WIDTH] = framebuffer[read_buffer];
                for (int x = 0; x < GB_WIDTH; x++) {
                    uint8_t pixel = fb[y][x] & 0x03;
                    // 8-bit gray levels
                    static const uint8_t gray[4] = {255, 180, 110, 70};
                    set_composite_level(gray[pixel]);
                    asm volatile(
                        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                    );
                }
            }
            
            // Front porch
            set_composite_level(PWM_BLANK);
            busy_wait_us_32(FRONT_PORCH_US);
        }
        
        // === Post-blank lines ===
        for (int line = 0; line < POST_BLANK_LINES; line++) {
            set_composite_level(PWM_SYNC);
            busy_wait_us_32(HSYNC_US);
            set_composite_level(PWM_BLANK);
            busy_wait_us_32(LINE_TIME_US - HSYNC_US);
        }
    }
}

// ============================================================================
// Core 0: Capture Game Boy video - Pixel clock synced
// ============================================================================

bool capture_gb_frame(void) {
    uint8_t (*buffer)[GB_WIDTH] = framebuffer[write_buffer];
    int timeout_count;
    
    // Wait for VSYNC to go LOW (indicates vblank period)
    timeout_count = 0;
    while (gpio_get(GB_VSYNC_PIN) != 0 && timeout_count < 100000) {
        timeout_count++;
    }
    if (timeout_count >= 100000) return false;
    
    // Wait for VSYNC to go HIGH (vblank ended, active video starting)
    timeout_count = 0;
    while (gpio_get(GB_VSYNC_PIN) != 1 && timeout_count < 100000) {
        timeout_count++;
    }
    if (timeout_count >= 100000) return false;
    
    // Capture 144 lines
    for (int y = 0; y < GB_HEIGHT; y++) {
        // Wait for HSYNC to go LOW (sync pulse start)
        timeout_count = 0;
        while (gpio_get(GB_HSYNC_PIN) != 0 && timeout_count < 5000) {
            timeout_count++;
        }
        if (timeout_count >= 5000) continue;
        
        // Wait for HSYNC to go HIGH (sync pulse end)
        timeout_count = 0;
        while (gpio_get(GB_HSYNC_PIN) != 1 && timeout_count < 5000) {
            timeout_count++;
        }
        
        // Sample 160 pixels synced to pixel clock
        for (int x = 0; x < GB_WIDTH; x++) {
            // Wait for pixel clock to go LOW
            timeout_count = 0;
            while (gpio_get(GB_PIXEL_CLOCK_PIN) != 0 && timeout_count < 100) {
                timeout_count++;
            }
            // Wait for pixel clock to go HIGH (rising edge)
            timeout_count = 0;
            while (gpio_get(GB_PIXEL_CLOCK_PIN) != 1 && timeout_count < 100) {
                timeout_count++;
            }
            
            // Read pixel data on rising edge
            buffer[y][x] = (gpio_get(GB_DATA_1_PIN) << 1) | gpio_get(GB_DATA_0_PIN);
        }
    }
    
    // Swap buffers
    write_buffer = 1 - write_buffer;
    read_buffer = 1 - read_buffer;
    frames_captured++;
    return true;
}

// ============================================================================
// Main
// ============================================================================
int main(void) {
    stdio_init_all();
    
    // LED setup
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    // Boot blink: 3 rapid flashes
    for (int i = 0; i < 3; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(100);
        gpio_put(LED_PIN, 0);
        sleep_ms(100);
    }
    
    printf("\n=== DMG to Composite Converter ===\n");
    printf("Starts in TEST PATTERN mode\n");
    printf("Double-tap BOOTSEL to toggle test pattern\n");
    
    // Initialize framebuffer to checkerboard
    memset(framebuffer, 0, sizeof(framebuffer));
    
    // Init semaphore
    sem_init(&video_ready, 0, 1);
    
    // Start video output on Core 1
    multicore_launch_core1(core1_video_output);
    sem_acquire_blocking(&video_ready);
    
    printf("Video output started on Core 1\n");
    
    // Set up Game Boy input pins
    gpio_init(GB_HSYNC_PIN);
    gpio_init(GB_VSYNC_PIN);
    gpio_init(GB_PIXEL_CLOCK_PIN);
    gpio_init(GB_DATA_0_PIN);
    gpio_init(GB_DATA_1_PIN);
    
    gpio_set_dir(GB_HSYNC_PIN, GPIO_IN);
    gpio_set_dir(GB_VSYNC_PIN, GPIO_IN);
    gpio_set_dir(GB_PIXEL_CLOCK_PIN, GPIO_IN);
    gpio_set_dir(GB_DATA_0_PIN, GPIO_IN);
    gpio_set_dir(GB_DATA_1_PIN, GPIO_IN);
    
    // No pull-ups or pull-downs - let GB drive the signals directly
    gpio_disable_pulls(GB_HSYNC_PIN);
    gpio_disable_pulls(GB_VSYNC_PIN);
    gpio_disable_pulls(GB_PIXEL_CLOCK_PIN);
    gpio_disable_pulls(GB_DATA_0_PIN);
    gpio_disable_pulls(GB_DATA_1_PIN);
    
    printf("Game Boy inputs ready\\n");
    
    // LED solid on = ready
    gpio_put(LED_PIN, 1);
    
    printf("Running...\n\n");
    
    // Variables for double-tap detection
    uint32_t last_tap_time = 0;
    bool button_was_pressed = false;
    
    // Variables for GB signal detection
    uint32_t no_signal_count = 0;
    
    // Main loop
    while (true) {
        // Check for BOOTSEL double-tap to toggle test pattern
        bool button_pressed = get_bootsel_button();
        if (button_pressed && !button_was_pressed) {
            // Button just pressed
            uint32_t now = time_us_32();
            if (now - last_tap_time < 500000) {  // 500ms window for double-tap
                // Double tap detected! Toggle test pattern mode
                test_pattern_mode = !test_pattern_mode;
                printf("Mode: %s\n", test_pattern_mode ? "TEST PATTERN" : "GAME BOY");
                
                // Blink LED to confirm
                gpio_put(LED_PIN, 0);
                sleep_ms(100);
                gpio_put(LED_PIN, 1);
                sleep_ms(100);
                gpio_put(LED_PIN, 0);
                sleep_ms(100);
                gpio_put(LED_PIN, 1);
                
                last_tap_time = 0;  // Reset to prevent triple-tap
            } else {
                last_tap_time = now;
            }
        }
        button_was_pressed = button_pressed;
        
        if (!test_pattern_mode) {
            // Try to capture a frame
            capture_gb_frame();
            frames_captured++;
            
            // Blink LED to show we're capturing
            if (frames_captured % 30 == 0) {
                gpio_put(LED_PIN, !gpio_get(LED_PIN));
            }
        } else {
            // In test mode - just wait, don't auto-detect
            // Use double-tap BOOTSEL to switch to GB mode manually
            sleep_ms(50);
        }
    }
    
    return 0;
}
