#include "logger.h"
#include "time_manager.h"
#include <Arduino.h>
#include <math.h>

static LogEntry g_buffer[LOG_MAX_EVENTS];
static uint16_t g_head = 0;      // Next write position
static uint16_t g_count = 0;    // Current count
static bool g_initialized = false;

void logger_init(void) {
    g_head = 0;
    g_count = 0;
    g_initialized = true;
}

void logger_log_event(InputEventType event, const Features* features,
                      const BrainOutput* brain_output, const PetState* state_after) {
    if (!g_initialized) return;

    LogEntry* entry = &g_buffer[g_head];

    // Timestamp (use millis if no RTC)
    entry->timestamp = millis() / 1000;
    entry->input_event = event;

    if (features) {
        entry->features = *features;
    }

    if (brain_output) {
        entry->model_action = brain_output->action_id;
        entry->model_valence = brain_output->valence;
        entry->model_arousal = brain_output->arousal;
    }

    if (state_after) {
        entry->state_after = *state_after;
    }

    // Advance head (ring buffer)
    g_head = (g_head + 1) % LOG_MAX_EVENTS;
    if (g_count < LOG_MAX_EVENTS) {
        g_count++;
    }
}

uint16_t logger_get_count(void) {
    return g_count;
}

bool logger_get_event(uint16_t index, LogEntry* entry) {
    if (!g_initialized || index >= g_count || !entry) {
        return false;
    }

    // Calculate actual position (oldest first)
    uint16_t start = (g_count == LOG_MAX_EVENTS) ? g_head : 0;
    uint16_t pos = (start + index) % LOG_MAX_EVENTS;

    *entry = g_buffer[pos];
    return true;
}

uint32_t logger_to_json(char* buffer, uint32_t buffer_size, uint16_t start, uint16_t count) {
    if (!g_initialized || !buffer || buffer_size < 3) {
        return 0;
    }

    uint32_t written = 0;
    written += snprintf(buffer + written, buffer_size - written, "[");

    uint16_t actual_count = (count > g_count) ? g_count : count;
    if (start >= g_count) {
        actual_count = 0;
    } else if (start + actual_count > g_count) {
        actual_count = g_count - start;
    }

    for (uint16_t i = 0; i < actual_count; i++) {
        LogEntry entry;
        if (!logger_get_event(start + i, &entry)) continue;

        if (i > 0) {
            written += snprintf(buffer + written, buffer_size - written, ",");
        }

        // Check if we have enough space (rough estimate)
        if (buffer_size - written < 400) {
            break;
        }

        written += snprintf(buffer + written, buffer_size - written,
            "{"
            "\"ts\":%lu,"
            "\"event\":%d,"
            "\"features\":{"
                "\"hunger\":%.3f,"
                "\"energy\":%.3f,"
                "\"affection\":%.3f,"
                "\"trust\":%.3f,"
                "\"stress\":%.3f,"
                "\"dt\":%.3f,"
                "\"feed_5m\":%.3f,"
                "\"pet_5m\":%.3f,"
                "\"ignore\":%.3f,"
                "\"tod_sin\":%.3f,"
                "\"tod_cos\":%.3f,"
                "\"spam\":%.3f"
            "},"
            "\"brain\":{\"action\":%d,\"valence\":%.3f,\"arousal\":%.3f},"
            "\"state\":{\"hunger\":%.3f,\"energy\":%.3f,\"affection\":%.3f,\"trust\":%.3f,\"stress\":%.3f}"
            "}",
            entry.timestamp,
            (int)entry.input_event,
            entry.features.hunger,
            entry.features.energy,
            entry.features.affection_need,
            entry.features.trust,
            entry.features.stress,
            entry.features.dt_seconds_norm,
            entry.features.feed_count_5m_norm,
            entry.features.pet_count_5m_norm,
            entry.features.ignore_time_norm,
            entry.features.time_of_day_sin,
            entry.features.time_of_day_cos,
            entry.features.spam_score_norm,
            (int)entry.model_action,
            entry.model_valence,
            entry.model_arousal,
            entry.state_after.hunger,
            entry.state_after.energy,
            entry.state_after.affection_need,
            entry.state_after.trust,
            entry.state_after.stress
        );
    }

    written += snprintf(buffer + written, buffer_size - written, "]");
    return written;
}

void logger_clear(void) {
    g_head = 0;
    g_count = 0;
}

void logger_build_features(Features* features, const PetState* state,
                           const InteractionStats* stats, uint32_t current_ms) {
    if (!features || !state || !stats) return;

    // Direct state features
    features->hunger = state->hunger;
    features->energy = state->energy;
    features->affection_need = state->affection_need;
    features->trust = state->trust;
    features->stress = state->stress;

    // Time since last interaction (normalize to ~10 minutes max)
    uint32_t dt_ms = current_ms - stats->last_interaction_ms;
    features->dt_seconds_norm = fminf(1.0f, (dt_ms / 1000.0f) / 600.0f);

    // Interaction counts (normalize assuming max 10 in window)
    features->feed_count_5m_norm = fminf(1.0f, stats->feed_count_5m / 10.0f);
    features->pet_count_5m_norm = fminf(1.0f, stats->pet_count_5m / 10.0f);

    // Ignore time (time since any interaction started being "too long")
    if (stats->ignore_start_ms > 0) {
        uint32_t ignore_ms = current_ms - stats->ignore_start_ms;
        features->ignore_time_norm = fminf(1.0f, (ignore_ms / 1000.0f) / 300.0f);
    } else {
        features->ignore_time_norm = 0.0f;
    }

    // Time of day (using time_manager for real time)
    time_get_features(&features->time_of_day_sin, &features->time_of_day_cos);

    // Spam score
    features->spam_score_norm = stats->spam_score;
}
