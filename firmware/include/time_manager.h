#pragma once
#include <stdint.h>

// Initialize time manager, loads offset from NVS
void time_init(void);

// Set current time (hour 0-23, minute 0-59)
void time_set(uint8_t hour, uint8_t minute);

// Get current hour (0-23) and minute (0-59)
void time_get(uint8_t* hour, uint8_t* minute);

// Get time of day as normalized values for neural network
// Returns sin and cos of the hour angle (cyclical encoding)
void time_get_features(float* sin_out, float* cos_out);

// Check if it's "night" time (22:00 - 07:00)
bool time_is_night(void);

// Save time offset to NVS
void time_save(void);
