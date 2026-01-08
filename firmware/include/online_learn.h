#pragma once
#include "logger.h"
#include "brain_infer.h"

// Initialize online learning system
void online_learn_init(void);

// Called when owner takes an action (feeds or pets)
// This reinforces the current state -> action mapping
// reward_action: the action ID that should be reinforced
// features: current state features when action was taken
void online_learn_reward(uint8_t reward_action, const Features* features);

// Called periodically to apply learned adjustments
// Returns true if weights were updated
bool online_learn_apply(void);

// Save learned adjustments to NVS
void online_learn_save(void);

// Get learning stats
uint32_t online_learn_get_reward_count(void);

// Get the learned bias for an action (called by brain_infer)
float online_learn_get_bias(uint8_t action);
