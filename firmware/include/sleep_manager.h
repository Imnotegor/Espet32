#pragma once
#include <stdint.h>

// Initialize sleep manager
void sleep_init(uint8_t wakeup_pin1, uint8_t wakeup_pin2);

// Request device to go to deep sleep
// Will save state, show sleep animation, then enter deep sleep
// Device wakes up on button press
void sleep_request(void);

// Check if we just woke up from deep sleep
bool sleep_was_sleeping(void);

// Get sleep duration in seconds (if woke from sleep)
uint32_t sleep_get_duration(void);
