#include "buttons.h"
#include <Arduino.h>

static ButtonConfig g_config;
static ButtonState g_state[BUTTON_COUNT];
static uint8_t g_pins[BUTTON_COUNT];
static ButtonEvent g_pending_event;
static bool g_has_pending_event = false;
static ButtonEventCallback g_callback = nullptr;

void buttons_config_init(ButtonConfig* config) {
    config->debounce_ms = 50;
    config->long_press_ms = 500;
    config->double_press_ms = 300;
}

void buttons_init(uint8_t feed_pin, uint8_t pet_pin, const ButtonConfig* config) {
    g_pins[BUTTON_FEED] = feed_pin;
    g_pins[BUTTON_PET] = pet_pin;

    if (config) {
        g_config = *config;
    } else {
        buttons_config_init(&g_config);
    }

    // Initialize pins as input with pullup
    pinMode(feed_pin, INPUT_PULLUP);
    pinMode(pet_pin, INPUT_PULLUP);

    // Initialize state
    for (int i = 0; i < BUTTON_COUNT; i++) {
        g_state[i].current_state = false;
        g_state[i].last_raw_state = false;
        g_state[i].last_change_ms = 0;
        g_state[i].press_start_ms = 0;
        g_state[i].press_count = 0;
        g_state[i].pending_event = false;
        g_state[i].release_time_ms = 0;
    }

    g_has_pending_event = false;
}

static void emit_event(ButtonId button, GestureType gesture, uint32_t timestamp) {
    ButtonEvent event;
    event.button = button;
    event.gesture = gesture;
    event.timestamp_ms = timestamp;

    if (g_callback) {
        g_callback(event);
    } else {
        g_pending_event = event;
        g_has_pending_event = true;
    }
}

bool buttons_update(uint32_t current_ms) {
    bool event_generated = false;

    for (int i = 0; i < BUTTON_COUNT; i++) {
        ButtonState* state = &g_state[i];
        // Read pin (active low with pullup)
        bool raw = (digitalRead(g_pins[i]) == LOW);

        // Debounce
        if (raw != state->last_raw_state) {
            state->last_change_ms = current_ms;
            state->last_raw_state = raw;
        }

        bool debounced = state->current_state;
        if ((current_ms - state->last_change_ms) >= g_config.debounce_ms) {
            debounced = raw;
        }

        // State change detection
        bool was_pressed = state->current_state;
        state->current_state = debounced;

        // Button just pressed
        if (!was_pressed && debounced) {
            state->press_start_ms = current_ms;
            state->press_count++;
        }

        // Button just released
        if (was_pressed && !debounced) {
            uint32_t press_duration = current_ms - state->press_start_ms;

            if (press_duration >= g_config.long_press_ms) {
                // Long press detected
                emit_event((ButtonId)i, GESTURE_LONG, current_ms);
                state->press_count = 0;
                state->pending_event = false;
                event_generated = true;
            } else {
                // Short press - but wait to check for double
                state->release_time_ms = current_ms;
                state->pending_event = true;
            }
        }

        // Check for double press timeout
        if (state->pending_event && !state->current_state) {
            uint32_t since_release = current_ms - state->release_time_ms;

            if (state->press_count >= 2) {
                // Double press detected
                emit_event((ButtonId)i, GESTURE_DOUBLE, current_ms);
                state->press_count = 0;
                state->pending_event = false;
                event_generated = true;
            } else if (since_release >= g_config.double_press_ms) {
                // Timeout - single short press
                emit_event((ButtonId)i, GESTURE_SHORT, current_ms);
                state->press_count = 0;
                state->pending_event = false;
                event_generated = true;
            }
        }
    }

    return event_generated;
}

bool buttons_get_event(ButtonEvent* event) {
    if (g_has_pending_event && event) {
        *event = g_pending_event;
        g_has_pending_event = false;
        return true;
    }
    return false;
}

void buttons_set_callback(ButtonEventCallback callback) {
    g_callback = callback;
}

bool buttons_is_pressed(ButtonId button) {
    if (button < BUTTON_COUNT) {
        return g_state[button].current_state;
    }
    return false;
}

void buttons_get_state(ButtonId button, ButtonState* state) {
    if (button < BUTTON_COUNT && state) {
        *state = g_state[button];
    }
}
