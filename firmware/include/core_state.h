#ifndef CORE_STATE_H
#define CORE_STATE_H

#include <stdint.h>

// Pet state structure - all values normalized to 0.0 - 1.0
typedef struct {
    float hunger;          // 0 = full, 1 = starving
    float energy;          // 0 = exhausted, 1 = fully rested
    float affection_need;  // 0 = satisfied, 1 = desperate for attention
    float trust;           // 0 = distrust, 1 = full trust
    float stress;          // 0 = calm, 1 = very stressed
} PetState;

// Action IDs that the brain outputs
typedef enum {
    ACTION_SLEEP = 0,
    ACTION_IDLE = 1,
    ACTION_PLAY = 2,
    ACTION_ASK_FOOD = 3,
    ACTION_ASK_PET = 4,
    ACTION_HAPPY = 5,
    ACTION_ANNOYED = 6,
    ACTION_SAD = 7,
    ACTION_COUNT = 8
} PetAction;

// Brain output structure
typedef struct {
    PetAction action_id;
    float valence;   // -1 (negative) to 1 (positive)
    float arousal;   // 0 (calm) to 1 (excited)
} BrainOutput;

// Interaction statistics for feature calculation
typedef struct {
    uint32_t last_interaction_ms;     // timestamp of last interaction
    uint16_t feed_count_1m;           // feed count in last 1 minute
    uint16_t feed_count_5m;           // feed count in last 5 minutes
    uint16_t pet_count_1m;            // pet count in last 1 minute
    uint16_t pet_count_5m;            // pet count in last 5 minutes
    uint32_t ignore_start_ms;         // when ignore period started
    float spam_score;                 // 0-1, rapid button presses
} InteractionStats;

// Configuration for state update rates
typedef struct {
    float hunger_rate;          // hunger increase per second
    float energy_decay_rate;    // energy decrease per second when awake
    float energy_regen_rate;    // energy increase per second when sleeping
    float affection_decay_rate; // affection need increase per second
    float stress_decay_rate;    // stress natural decrease per second
    float trust_decay_rate;     // trust natural slow decrease

    float feed_hunger_reduction;    // hunger reduction per feed
    float feed_stress_reduction;    // stress reduction per feed
    float pet_affection_reduction;  // affection need reduction per pet
    float pet_stress_reduction;     // stress reduction per pet

    float spam_penalty;         // effect reduction when spam_score is high
} StateConfig;

// Initialize state to defaults
void core_state_init(PetState* state);

// Initialize configuration with default values
void core_state_config_init(StateConfig* config);

// Update state based on time delta (called every tick)
void core_state_update(PetState* state, const StateConfig* config,
                       uint32_t delta_ms, PetAction current_action);

// Apply feed interaction
void core_state_feed(PetState* state, const StateConfig* config,
                     InteractionStats* stats);

// Apply pet interaction
void core_state_pet(PetState* state, const StateConfig* config,
                    InteractionStats* stats);

// Update interaction statistics (call periodically)
void core_state_update_stats(InteractionStats* stats, uint32_t current_ms);

// Initialize interaction stats
void core_state_stats_init(InteractionStats* stats);

// Clamp value to 0-1 range
float clamp01(float value);

// Calculate trust change based on context
float calculate_trust_change(const PetState* state, bool was_requested,
                             bool was_timely);

#endif // CORE_STATE_H
