#ifndef RGB_RENDERER_H
#define RGB_RENDERER_H

#include <stdint.h>
#include "core_state.h"

// RGB color structure
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RGBColor;

// Renderer output (for logging/UI)
typedef struct {
    RGBColor color;
    float pulse;        // 0-1, current pulse phase
    float brightness;   // 0-1
} RGBOutput;

// Dual LED output (for DualKey board)
typedef struct {
    RGBColor hunger_led;  // LED 0: hunger indicator (red->blue)
    RGBColor mood_led;    // LED 1: mood indicator (yellow->green)
} DualLEDOutput;

// Initialize RGB renderer with LED pin (legacy single LED)
void rgb_init(uint8_t pin);

// Initialize for ESP-DualKey board with power control and dual LEDs
void rgb_init_dualkey(uint8_t data_pin, uint8_t power_pin, uint8_t led_count);

// Update RGB based on pet state and brain output
// Call this frequently for smooth animations (~50Hz)
void rgb_update(const PetState* state, const BrainOutput* brain, uint32_t current_ms);

// Get current RGB output (for UI/logging)
void rgb_get_output(RGBOutput* output);

// Get dual LED output
void rgb_get_dual_output(DualLEDOutput* output);

// Manual override (for testing/effects)
void rgb_set_override(const RGBColor* color, uint32_t duration_ms);

// Clear override
void rgb_clear_override(void);

// Flash effect (for button feedback)
void rgb_flash(const RGBColor* color, uint16_t duration_ms);

// Flash specific LED (0 = hunger/feed, 1 = mood/pet)
void rgb_flash_led(uint8_t led_index, const RGBColor* color, uint16_t duration_ms);

// Set both LEDs directly (bypasses animation - for sleep effects)
void rgb_set_both_leds(const RGBColor* led0, const RGBColor* led1);

// Turn off LEDs and power (for deep sleep)
void rgb_power_off(void);

// Map valence/arousal to color (utility function)
RGBColor rgb_emotion_to_color(float valence, float arousal);

// Map action to base color (utility function)
RGBColor rgb_action_to_color(PetAction action);

// Map hunger level to color (red=hungry -> blue=full)
RGBColor rgb_hunger_to_color(float hunger);

// Map mood (valence) to color (yellow=bad -> green=good)
RGBColor rgb_mood_to_color(float valence);

#endif // RGB_RENDERER_H
