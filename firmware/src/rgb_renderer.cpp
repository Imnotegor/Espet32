#include "rgb_renderer.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

static Adafruit_NeoPixel* g_led = nullptr;
static uint8_t g_power_pin = 0;
static uint8_t g_led_count = 1;
static bool g_dualkey_mode = false;

static RGBOutput g_current_output;
static DualLEDOutput g_dual_output;

static bool g_override_active = false;
static RGBColor g_override_color;
static uint32_t g_override_end_ms = 0;

// Per-LED flash state
static bool g_flash_active[2] = {false, false};
static RGBColor g_flash_color[2];
static uint32_t g_flash_end_ms[2] = {0, 0};

// Animation state
static float g_pulse_phase = 0.0f;
static uint32_t g_last_update_ms = 0;

void rgb_init(uint8_t pin) {
    if (g_led) {
        delete g_led;
    }
    g_led = new Adafruit_NeoPixel(1, pin, NEO_GRB + NEO_KHZ800);
    g_led->begin();
    g_led->setBrightness(50);
    g_led_count = 1;
    g_dualkey_mode = false;

    g_current_output.color = {0, 0, 0};
    g_current_output.pulse = 0.0f;
    g_current_output.brightness = 0.5f;

    g_override_active = false;
    g_flash_active[0] = false;
    g_flash_active[1] = false;
    g_pulse_phase = 0.0f;
    g_last_update_ms = 0;
}

void rgb_init_dualkey(uint8_t data_pin, uint8_t power_pin, uint8_t led_count) {
    if (g_led) {
        delete g_led;
    }

    // Enable power to WS2812
    g_power_pin = power_pin;
    pinMode(g_power_pin, OUTPUT);
    digitalWrite(g_power_pin, HIGH);
    delay(10); // Allow power to stabilize

    g_led_count = led_count > 0 ? led_count : 2;
    g_dualkey_mode = true;

    g_led = new Adafruit_NeoPixel(g_led_count, data_pin, NEO_GRB + NEO_KHZ800);
    g_led->begin();
    g_led->setBrightness(80); // Higher brightness for DualKey

    g_current_output.color = {0, 0, 0};
    g_current_output.pulse = 0.0f;
    g_current_output.brightness = 0.5f;

    g_dual_output.hunger_led = {0, 0, 0};
    g_dual_output.mood_led = {0, 0, 0};

    g_override_active = false;
    g_flash_active[0] = false;
    g_flash_active[1] = false;
    g_pulse_phase = 0.0f;
    g_last_update_ms = 0;

    // Initial color test: brief flash both LEDs
    g_led->setPixelColor(0, g_led->Color(0, 0, 100)); // Blue for hunger LED
    g_led->setPixelColor(1, g_led->Color(0, 100, 0)); // Green for mood LED
    g_led->show();
    delay(200);
    g_led->clear();
    g_led->show();
}

// Helper: HSV to RGB conversion
static RGBColor hsv_to_rgb(float h, float s, float v) {
    RGBColor rgb;
    if (s <= 0.0f) {
        rgb.r = rgb.g = rgb.b = (uint8_t)(v * 255);
        return rgb;
    }

    float hh = fmodf(h, 360.0f) / 60.0f;
    int i = (int)hh;
    float ff = hh - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - (s * ff));
    float t = v * (1.0f - (s * (1.0f - ff)));

    float r, g, b;
    switch(i) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }

    rgb.r = (uint8_t)(r * 255);
    rgb.g = (uint8_t)(g * 255);
    rgb.b = (uint8_t)(b * 255);
    return rgb;
}

// Hunger LED: red (hungry, hunger=1) -> blue (full, hunger=0)
RGBColor rgb_hunger_to_color(float hunger) {
    // Clamp hunger to 0-1
    if (hunger < 0.0f) hunger = 0.0f;
    if (hunger > 1.0f) hunger = 1.0f;

    // Invert: full (0) = blue, hungry (1) = red
    // Red: 0 degrees, Blue: 240 degrees
    // As hunger increases: blue -> purple -> red
    float hue = 240.0f - hunger * 240.0f; // 240 (blue) when hunger=0, 0 (red) when hunger=1

    float saturation = 0.9f;
    float value = 0.7f + hunger * 0.3f; // Brighter when hungry

    return hsv_to_rgb(hue, saturation, value);
}

// Mood LED: purple (bad mood, valence=-1) -> green (good mood, valence=1)
RGBColor rgb_mood_to_color(float valence) {
    // Clamp valence to -1 to 1
    if (valence < -1.0f) valence = -1.0f;
    if (valence > 1.0f) valence = 1.0f;

    // Normalize to 0-1
    float norm = (valence + 1.0f) / 2.0f; // 0 = bad mood, 1 = good mood

    // Purple: 280 degrees, Green: 120 degrees
    // As mood improves: purple -> blue -> cyan -> green
    // Going backwards through hue: 280 -> 120 (160 degree span)
    float hue = 280.0f - norm * 160.0f; // 280 (purple) at norm=0, 120 (green) at norm=1

    float saturation = 0.85f;
    float value = 0.6f + norm * 0.4f; // Brighter when happy

    return hsv_to_rgb(hue, saturation, value);
}

RGBColor rgb_emotion_to_color(float valence, float arousal) {
    // Valence: -1 (negative/red) to 1 (positive/green-blue)
    // Arousal: 0 (calm/dim) to 1 (excited/bright)

    float norm_valence = (valence + 1.0f) / 2.0f;
    float hue = 0.0f;

    if (norm_valence < 0.5f) {
        hue = norm_valence * 2.0f * 60.0f;
    } else {
        hue = 60.0f + (norm_valence - 0.5f) * 2.0f * 120.0f;
    }

    float saturation = 0.6f + arousal * 0.4f;
    float value = 0.3f + arousal * 0.7f;

    return hsv_to_rgb(hue, saturation, value);
}

RGBColor rgb_action_to_color(PetAction action) {
    switch (action) {
        case ACTION_SLEEP:
            return {20, 20, 60};
        case ACTION_IDLE:
            return {50, 80, 50};
        case ACTION_PLAY:
            return {80, 200, 80};
        case ACTION_ASK_FOOD:
            return {200, 100, 0};
        case ACTION_ASK_PET:
            return {180, 100, 180};
        case ACTION_HAPPY:
            return {100, 255, 100};
        case ACTION_ANNOYED:
            return {255, 80, 0};
        case ACTION_SAD:
            return {50, 50, 150};
        default:
            return {100, 100, 100};
    }
}

void rgb_update(const PetState* state, const BrainOutput* brain, uint32_t current_ms) {
    if (!g_led) return;

    uint32_t dt = current_ms - g_last_update_ms;
    if (dt > 1000) dt = 1000;
    g_last_update_ms = current_ms;
    float dt_sec = dt / 1000.0f;

    // Update pulse phase
    float pulse_speed = 0.5f + brain->arousal * 2.5f;
    g_pulse_phase += pulse_speed * dt_sec;
    if (g_pulse_phase > 1.0f) g_pulse_phase -= 1.0f;

    // Calculate pulse factor
    float pulse_factor = 1.0f;
    if (brain->arousal > 0.3f) {
        float pulse_amount = brain->arousal * 0.3f;
        pulse_factor = 1.0f - pulse_amount * (0.5f + 0.5f * sinf(g_pulse_phase * 2.0f * M_PI));
    }

    // Sleep breathing effect
    if (brain->action_id == ACTION_SLEEP) {
        pulse_factor = 0.3f + 0.7f * (0.5f + 0.5f * sinf(current_ms * 0.002f));
    }

    if (g_dualkey_mode && g_led_count >= 2) {
        // Dual LED mode
        RGBColor hunger_color = rgb_hunger_to_color(state->hunger);
        RGBColor mood_color = rgb_mood_to_color(brain->valence);

        float action_mod0 = 1.0f;
        float action_mod1 = 1.0f;

        switch (brain->action_id) {
            case ACTION_SLEEP:
                // Slow breathing, very dim
                {
                    float breath = 0.15f + 0.25f * (0.5f + 0.5f * sinf(current_ms * 0.001f));
                    action_mod0 = breath;
                    action_mod1 = breath;
                    // Add blue tint for sleep
                    hunger_color.b = (uint8_t)fminf(255, hunger_color.b + 40);
                    mood_color.b = (uint8_t)fminf(255, mood_color.b + 40);
                }
                break;

            case ACTION_IDLE:
                // Normal display with gentle pulse
                action_mod0 = 0.7f + 0.3f * pulse_factor;
                action_mod1 = 0.7f + 0.3f * pulse_factor;
                break;

            case ACTION_PLAY:
                // Fast alternating blinks, bright and fun!
                {
                    float play_phase = sinf(current_ms * 0.015f);
                    action_mod0 = 0.5f + 0.5f * (play_phase > 0 ? 1.0f : 0.3f);
                    action_mod1 = 0.5f + 0.5f * (play_phase > 0 ? 0.3f : 1.0f);
                    // Brighten colors
                    hunger_color.g = (uint8_t)fminf(255, hunger_color.g + 50);
                    mood_color.g = (uint8_t)fminf(255, mood_color.g + 50);
                }
                break;

            case ACTION_ASK_FOOD:
                // Hunger LED pulses urgently, mood LED dim
                {
                    float ask_pulse = 0.4f + 0.6f * fabsf(sinf(current_ms * 0.012f));
                    action_mod0 = ask_pulse;
                    action_mod1 = 0.3f;
                    // Orange tint on hunger LED
                    hunger_color.r = (uint8_t)fminf(255, hunger_color.r + 80);
                    hunger_color.g = (uint8_t)fminf(255, hunger_color.g + 30);
                }
                break;

            case ACTION_ASK_PET:
                // Mood LED pulses to attract attention, hunger LED dim
                {
                    float ask_pulse = 0.4f + 0.6f * fabsf(sinf(current_ms * 0.010f));
                    action_mod0 = 0.3f;
                    action_mod1 = ask_pulse;
                    // Pink tint on mood LED
                    mood_color.r = (uint8_t)fminf(255, mood_color.r + 60);
                    mood_color.b = (uint8_t)fminf(255, mood_color.b + 40);
                }
                break;

            case ACTION_HAPPY:
                // Both LEDs bright with quick sparkle
                {
                    float sparkle = 0.8f + 0.2f * sinf(current_ms * 0.02f);
                    action_mod0 = sparkle;
                    action_mod1 = sparkle;
                    // Green/yellow happy tint
                    hunger_color.g = (uint8_t)fminf(255, hunger_color.g + 60);
                    mood_color.g = (uint8_t)fminf(255, mood_color.g + 80);
                }
                break;

            case ACTION_ANNOYED:
                // Red tint, irregular flickering
                {
                    float flicker = 0.5f + 0.5f * sinf(current_ms * 0.025f + sinf(current_ms * 0.007f) * 3.0f);
                    action_mod0 = flicker;
                    action_mod1 = flicker;
                    // Red/orange angry tint
                    hunger_color.r = (uint8_t)fminf(255, hunger_color.r + 100);
                    mood_color.r = (uint8_t)fminf(255, mood_color.r + 100);
                    mood_color.g = (uint8_t)(mood_color.g * 0.5f);
                    mood_color.b = (uint8_t)(mood_color.b * 0.3f);
                }
                break;

            case ACTION_SAD:
                // Both dim with blue tint, slow pulse
                {
                    float sad_pulse = 0.2f + 0.3f * (0.5f + 0.5f * sinf(current_ms * 0.003f));
                    action_mod0 = sad_pulse;
                    action_mod1 = sad_pulse;
                    // Blue sad tint
                    hunger_color.r = (uint8_t)(hunger_color.r * 0.5f);
                    hunger_color.b = (uint8_t)fminf(255, hunger_color.b + 80);
                    mood_color.r = (uint8_t)(mood_color.r * 0.5f);
                    mood_color.b = (uint8_t)fminf(255, mood_color.b + 80);
                }
                break;

            default:
                break;
        }

        // Apply action modifiers
        hunger_color.r = (uint8_t)(hunger_color.r * action_mod0);
        hunger_color.g = (uint8_t)(hunger_color.g * action_mod0);
        hunger_color.b = (uint8_t)(hunger_color.b * action_mod0);

        mood_color.r = (uint8_t)(mood_color.r * action_mod1);
        mood_color.g = (uint8_t)(mood_color.g * action_mod1);
        mood_color.b = (uint8_t)(mood_color.b * action_mod1);

        // Store for output
        g_dual_output.hunger_led = hunger_color;
        g_dual_output.mood_led = mood_color;

        // Check for flash overrides per LED
        RGBColor led0_color = hunger_color;
        RGBColor led1_color = mood_color;

        if (g_flash_active[0]) {
            if (current_ms < g_flash_end_ms[0]) {
                led0_color = g_flash_color[0];
            } else {
                g_flash_active[0] = false;
            }
        }

        if (g_flash_active[1]) {
            if (current_ms < g_flash_end_ms[1]) {
                led1_color = g_flash_color[1];
            } else {
                g_flash_active[1] = false;
            }
        }

        // Check for global override
        if (g_override_active) {
            if (current_ms < g_override_end_ms) {
                led0_color = g_override_color;
                led1_color = g_override_color;
            } else {
                g_override_active = false;
            }
        }

        // Set LED colors
        g_led->setPixelColor(0, g_led->Color(led0_color.r, led0_color.g, led0_color.b));
        g_led->setPixelColor(1, g_led->Color(led1_color.r, led1_color.g, led1_color.b));
        g_led->show();

        // Also update legacy output (average of both)
        g_current_output.color.r = (led0_color.r + led1_color.r) / 2;
        g_current_output.color.g = (led0_color.g + led1_color.g) / 2;
        g_current_output.color.b = (led0_color.b + led1_color.b) / 2;
        g_current_output.pulse = g_pulse_phase;
        g_current_output.brightness = pulse_factor;

    } else {
        // Single LED mode
        RGBColor base_color = rgb_emotion_to_color(brain->valence, brain->arousal);
        RGBColor action_color = rgb_action_to_color(brain->action_id);

        float action_mix = 0.3f;
        base_color.r = (uint8_t)(base_color.r * (1.0f - action_mix) + action_color.r * action_mix);
        base_color.g = (uint8_t)(base_color.g * (1.0f - action_mix) + action_color.g * action_mix);
        base_color.b = (uint8_t)(base_color.b * (1.0f - action_mix) + action_color.b * action_mix);

        if (state->hunger > 0.7f) {
            float hunger_urgency = (state->hunger - 0.7f) / 0.3f;
            float flicker = 0.5f + 0.5f * sinf(current_ms * 0.01f * (1.0f + hunger_urgency));
            uint8_t red_add = (uint8_t)(hunger_urgency * 80.0f * flicker);
            base_color.r = (uint8_t)fminf(255, base_color.r + red_add);
        }

        float brightness = 0.7f * pulse_factor;
        if (state->stress > 0.6f) {
            brightness = fminf(1.0f, brightness + (state->stress - 0.6f) * 0.3f);
        }

        g_current_output.color = base_color;
        g_current_output.pulse = g_pulse_phase;
        g_current_output.brightness = brightness;

        RGBColor display_color = base_color;

        if (g_flash_active[0]) {
            if (current_ms < g_flash_end_ms[0]) {
                display_color = g_flash_color[0];
                brightness = 1.0f;
            } else {
                g_flash_active[0] = false;
            }
        }

        if (g_override_active) {
            if (current_ms < g_override_end_ms) {
                display_color = g_override_color;
            } else {
                g_override_active = false;
            }
        }

        uint8_t final_r = (uint8_t)(display_color.r * brightness);
        uint8_t final_g = (uint8_t)(display_color.g * brightness);
        uint8_t final_b = (uint8_t)(display_color.b * brightness);

        g_led->setPixelColor(0, g_led->Color(final_r, final_g, final_b));
        g_led->show();
    }
}

void rgb_get_output(RGBOutput* output) {
    if (output) {
        *output = g_current_output;
    }
}

void rgb_get_dual_output(DualLEDOutput* output) {
    if (output) {
        *output = g_dual_output;
    }
}

void rgb_set_override(const RGBColor* color, uint32_t duration_ms) {
    if (color) {
        g_override_color = *color;
        g_override_end_ms = millis() + duration_ms;
        g_override_active = true;
    }
}

void rgb_clear_override(void) {
    g_override_active = false;
}

void rgb_flash(const RGBColor* color, uint16_t duration_ms) {
    if (color) {
        // Flash all LEDs
        g_flash_color[0] = *color;
        g_flash_end_ms[0] = millis() + duration_ms;
        g_flash_active[0] = true;

        if (g_led_count >= 2) {
            g_flash_color[1] = *color;
            g_flash_end_ms[1] = millis() + duration_ms;
            g_flash_active[1] = true;
        }
    }
}

void rgb_flash_led(uint8_t led_index, const RGBColor* color, uint16_t duration_ms) {
    if (color && led_index < 2) {
        g_flash_color[led_index] = *color;
        g_flash_end_ms[led_index] = millis() + duration_ms;
        g_flash_active[led_index] = true;
    }
}

void rgb_set_both_leds(const RGBColor* led0, const RGBColor* led1) {
    if (!g_led) return;

    if (led0) {
        g_led->setPixelColor(0, g_led->Color(led0->r, led0->g, led0->b));
    }
    if (led1) {
        g_led->setPixelColor(1, g_led->Color(led1->r, led1->g, led1->b));
    }
    g_led->show();
}

void rgb_power_off(void) {
    if (!g_led) return;

    // Turn off all LEDs
    g_led->clear();
    g_led->show();

    // If using DualKey, disable power pin
    if (g_dualkey_mode && g_power_pin > 0) {
        digitalWrite(g_power_pin, LOW);
    }
}
