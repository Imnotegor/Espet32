#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include "core_state.h"

// Maximum events in ring buffer
#define LOG_MAX_EVENTS 100

// Input event types
typedef enum {
    INPUT_NONE = 0,
    INPUT_FEED_SHORT = 1,
    INPUT_FEED_LONG = 2,
    INPUT_FEED_DOUBLE = 3,
    INPUT_PET_SHORT = 4,
    INPUT_PET_LONG = 5,
    INPUT_PET_DOUBLE = 6,
    INPUT_IGNORE = 7  // No input for extended period
} InputEventType;

// Features snapshot (for training)
typedef struct {
    float hunger;
    float energy;
    float affection_need;
    float trust;
    float stress;
    float dt_seconds_norm;
    float feed_count_5m_norm;
    float pet_count_5m_norm;
    float ignore_time_norm;
    float time_of_day_sin;
    float time_of_day_cos;
    float spam_score_norm;
} Features;

// Log entry structure
typedef struct {
    uint32_t timestamp;           // Unix timestamp (or millis if no RTC)
    InputEventType input_event;
    Features features;
    PetAction model_action;
    float model_valence;
    float model_arousal;
    PetState state_after;         // State after event processed
} LogEntry;

// Initialize logger
void logger_init(void);

// Log an event
void logger_log_event(InputEventType event, const Features* features,
                      const BrainOutput* brain_output, const PetState* state_after);

// Get number of events in buffer
uint16_t logger_get_count(void);

// Get event by index (0 = oldest)
bool logger_get_event(uint16_t index, LogEntry* entry);

// Get events as JSON (for API)
// Returns number of bytes written, or required size if buffer too small
uint32_t logger_to_json(char* buffer, uint32_t buffer_size, uint16_t start, uint16_t count);

// Clear all events
void logger_clear(void);

// Build features from current state and stats
void logger_build_features(Features* features, const PetState* state,
                           const InteractionStats* stats, uint32_t current_ms);

#endif // LOGGER_H
