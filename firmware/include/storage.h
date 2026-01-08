#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "core_state.h"

// Model metadata
typedef struct {
    uint32_t version;          // Model version number
    uint32_t features_version; // Features schema version (must match)
    uint32_t size;             // Model blob size in bytes
    uint32_t crc32;            // CRC32 checksum
    uint32_t created_at;       // Unix timestamp
} ModelMeta;

// Pet statistics (for analytics)
typedef struct {
    uint32_t total_feeds;
    uint32_t total_pets;
    uint32_t total_playtime_sec;
    uint32_t max_trust_reached;     // scaled 0-1000
    uint32_t times_starved;         // hunger reached 1.0
    uint32_t boot_count;
    uint32_t last_save_timestamp;
} PetStats;

// Initialize storage (must call before any other storage functions)
bool storage_init(void);

// Save/load pet state
bool storage_save_state(const PetState* state);
bool storage_load_state(PetState* state);

// Save/load interaction stats
bool storage_save_interaction_stats(const InteractionStats* stats);
bool storage_load_interaction_stats(InteractionStats* stats);

// Save/load pet statistics
bool storage_save_stats(const PetStats* stats);
bool storage_load_stats(PetStats* stats);

// Model management
bool storage_save_model(const uint8_t* model_data, uint32_t size,
                        const ModelMeta* meta);
bool storage_load_model(uint8_t* model_data, uint32_t max_size,
                        uint32_t* actual_size);
bool storage_load_model_meta(ModelMeta* meta);
bool storage_has_valid_model(void);

// Check if fallback model should be used
bool storage_use_fallback_model(void);

// Reset to defaults (factory reset)
bool storage_reset_all(void);

// Get last error message
const char* storage_get_last_error(void);

// CRC32 utility
uint32_t storage_calc_crc32(const uint8_t* data, uint32_t length);

#endif // STORAGE_H
