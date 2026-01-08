#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>
#include <stdbool.h>

// Button IDs
typedef enum {
    BUTTON_FEED = 0,
    BUTTON_PET = 1,
    BUTTON_COUNT = 2
} ButtonId;

// Gesture types detected
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_SHORT = 1,      // < 500ms press
    GESTURE_LONG = 2,       // > 500ms press
    GESTURE_DOUBLE = 3      // two presses within 300ms
} GestureType;

// Button event structure
typedef struct {
    ButtonId button;
    GestureType gesture;
    uint32_t timestamp_ms;
} ButtonEvent;

// Configuration for button timing
typedef struct {
    uint16_t debounce_ms;       // debounce time (default: 50ms)
    uint16_t long_press_ms;     // threshold for long press (default: 500ms)
    uint16_t double_press_ms;   // max gap for double press (default: 300ms)
} ButtonConfig;

// Internal button state (per button)
typedef struct {
    bool current_state;         // current debounced state (true = pressed)
    bool last_raw_state;        // last raw reading
    uint32_t last_change_ms;    // last state change time
    uint32_t press_start_ms;    // when current press started
    uint8_t press_count;        // presses in sequence
    bool pending_event;         // waiting to determine gesture
    uint32_t release_time_ms;   // when released (for double-click detection)
} ButtonState;

// Callback type for button events
typedef void (*ButtonEventCallback)(ButtonEvent event);

// Initialize button system with pin configuration
// feed_pin and pet_pin should be GPIO numbers
void buttons_init(uint8_t feed_pin, uint8_t pet_pin, const ButtonConfig* config);

// Set default configuration
void buttons_config_init(ButtonConfig* config);

// Update button state - call this frequently (every 10-20ms)
// Returns true if an event was generated
bool buttons_update(uint32_t current_ms);

// Get pending event (if any) - returns true if event available
bool buttons_get_event(ButtonEvent* event);

// Register callback for button events
void buttons_set_callback(ButtonEventCallback callback);

// Check if button is currently pressed
bool buttons_is_pressed(ButtonId button);

// Get raw button state for debugging
void buttons_get_state(ButtonId button, ButtonState* state);

#endif // BUTTONS_H
