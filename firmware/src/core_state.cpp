#include "core_state.h"
#include <math.h>

float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

void core_state_init(PetState* state) {
    state->hunger = 0.3f;          // slightly hungry
    state->energy = 0.7f;          // fairly rested
    state->affection_need = 0.4f;  // moderate need
    state->trust = 0.5f;           // neutral trust
    state->stress = 0.2f;          // fairly calm
}

void core_state_config_init(StateConfig* config) {
    // Base rates per second
    config->hunger_rate = 0.001f;          // ~16 min to go from 0 to 1
    config->energy_decay_rate = 0.0008f;   // ~20 min when awake
    config->energy_regen_rate = 0.002f;    // ~8 min when sleeping
    config->affection_decay_rate = 0.0005f; // ~33 min
    config->stress_decay_rate = 0.0003f;   // natural stress decay
    config->trust_decay_rate = 0.00001f;   // very slow trust decay

    // Interaction effects
    config->feed_hunger_reduction = 0.4f;   // one feed removes 40% hunger
    config->feed_stress_reduction = 0.05f;  // small stress relief
    config->pet_affection_reduction = 0.35f; // one pet removes 35% need
    config->pet_stress_reduction = 0.1f;    // moderate stress relief

    config->spam_penalty = 0.7f;  // 70% reduction when spamming
}

void core_state_update(PetState* state, const StateConfig* config,
                       uint32_t delta_ms, PetAction current_action) {
    float dt = delta_ms / 1000.0f;

    // Hunger always increases
    state->hunger = clamp01(state->hunger + config->hunger_rate * dt);

    // Energy depends on action
    if (current_action == ACTION_SLEEP) {
        state->energy = clamp01(state->energy + config->energy_regen_rate * dt);
    } else {
        state->energy = clamp01(state->energy - config->energy_decay_rate * dt);
    }

    // Affection need increases over time
    state->affection_need = clamp01(state->affection_need +
                                    config->affection_decay_rate * dt);

    // Stress naturally decays but increases when hungry or low energy
    float stress_change = -config->stress_decay_rate * dt;
    if (state->hunger > 0.7f) {
        stress_change += 0.0005f * dt * (state->hunger - 0.7f) / 0.3f;
    }
    if (state->energy < 0.2f) {
        stress_change += 0.0003f * dt * (0.2f - state->energy) / 0.2f;
    }
    if (state->affection_need > 0.8f) {
        stress_change += 0.0002f * dt;
    }
    state->stress = clamp01(state->stress + stress_change);

    // Trust very slowly decays if not maintained
    state->trust = clamp01(state->trust - config->trust_decay_rate * dt);
}

void core_state_feed(PetState* state, const StateConfig* config,
                     InteractionStats* stats) {
    // Calculate effectiveness based on spam score
    float effectiveness = 1.0f - stats->spam_score * config->spam_penalty;

    // Apply hunger reduction
    float hunger_before = state->hunger;
    state->hunger = clamp01(state->hunger -
                            config->feed_hunger_reduction * effectiveness);

    // Reduce stress slightly
    state->stress = clamp01(state->stress -
                            config->feed_stress_reduction * effectiveness);

    // Trust increases if pet was actually hungry
    bool was_hungry = hunger_before > 0.5f;
    if (was_hungry && effectiveness > 0.5f) {
        state->trust = clamp01(state->trust + 0.02f);
    }

    // Feeding when not hungry can be annoying
    if (hunger_before < 0.2f) {
        state->stress = clamp01(state->stress + 0.03f);
    }

    // Update spam detection
    stats->spam_score = clamp01(stats->spam_score + 0.15f);
}

void core_state_pet(PetState* state, const StateConfig* config,
                    InteractionStats* stats) {
    // Calculate effectiveness based on spam score
    float effectiveness = 1.0f - stats->spam_score * config->spam_penalty;

    // Apply affection reduction
    float affection_before = state->affection_need;
    state->affection_need = clamp01(state->affection_need -
                                    config->pet_affection_reduction * effectiveness);

    // Reduce stress
    state->stress = clamp01(state->stress -
                            config->pet_stress_reduction * effectiveness);

    // Trust increases if pet needed affection
    bool needed_affection = affection_before > 0.5f;
    if (needed_affection && effectiveness > 0.5f) {
        state->trust = clamp01(state->trust + 0.015f);
    }

    // Petting when not needed can be slightly annoying
    if (affection_before < 0.15f) {
        state->stress = clamp01(state->stress + 0.02f);
    }

    // Update spam detection
    stats->spam_score = clamp01(stats->spam_score + 0.15f);
}

void core_state_stats_init(InteractionStats* stats) {
    stats->last_interaction_ms = 0;
    stats->feed_count_1m = 0;
    stats->feed_count_5m = 0;
    stats->pet_count_1m = 0;
    stats->pet_count_5m = 0;
    stats->ignore_start_ms = 0;
    stats->spam_score = 0.0f;
}

void core_state_update_stats(InteractionStats* stats, uint32_t current_ms) {
    // Decay spam score over time (half-life ~2 seconds)
    float decay = 0.0005f; // per ms
    uint32_t dt = current_ms - stats->last_interaction_ms;
    if (dt > 0 && dt < 60000) {
        stats->spam_score = clamp01(stats->spam_score - decay * dt);
    }

    // Note: feed_count and pet_count should be managed by time-window
    // tracking in the main application (using circular buffer or timestamps)
}

float calculate_trust_change(const PetState* state, bool was_requested,
                             bool was_timely) {
    float change = 0.0f;

    if (was_requested && was_timely) {
        change = 0.03f;  // Good response to request
    } else if (!was_requested && was_timely) {
        change = 0.01f;  // Anticipated need
    } else if (was_requested && !was_timely) {
        change = -0.01f; // Late response
    }
    // No change if neither requested nor timely

    // Trust change is slower when trust is already high/low
    if (state->trust > 0.8f && change > 0) {
        change *= 0.5f;
    }
    if (state->trust < 0.2f && change < 0) {
        change *= 0.5f;
    }

    return change;
}
