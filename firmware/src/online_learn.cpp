#include "online_learn.h"
#include "time_manager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

// Simple online learning using action bias adjustments
// We track biases for each action that get added to the model output
// Biases are learned based on owner's reinforcement (feeding/petting)

#define ACTION_COUNT 8
#define LEARNING_RATE 0.1f
#define DECAY_RATE 0.99f  // Slow decay of learned biases
#define MAX_BUFFER_SIZE 16

// Learned action biases (added to model output before softmax)
static float g_action_biases[ACTION_COUNT] = {0};

// Experience buffer for batch updates
struct Experience {
    float features[12];
    uint8_t action;
    float reward;  // Positive = reinforce, negative = discourage
};

static Experience g_buffer[MAX_BUFFER_SIZE];
static uint8_t g_buffer_count = 0;
static uint32_t g_total_rewards = 0;

static Preferences g_prefs;
static bool g_initialized = false;

// Simple hash of features to make learning state-dependent
static uint8_t hash_features(const Features* f) {
    // Discretize key features into bins
    uint8_t h = 0;
    h |= (uint8_t)(f->hunger * 3) & 0x03;           // 2 bits for hunger
    h |= ((uint8_t)(f->energy * 3) & 0x03) << 2;    // 2 bits for energy
    h |= ((uint8_t)(f->affection_need * 3) & 0x03) << 4;  // 2 bits for affection
    h |= (time_is_night() ? 1 : 0) << 6;            // 1 bit for night
    return h;
}

void online_learn_init(void) {
    // Load saved biases from NVS
    if (g_prefs.begin("learn", true)) {  // read-only
        for (int i = 0; i < ACTION_COUNT; i++) {
            char key[8];
            snprintf(key, sizeof(key), "b%d", i);
            g_action_biases[i] = g_prefs.getFloat(key, 0.0f);
        }
        g_total_rewards = g_prefs.getUInt("count", 0);
        g_prefs.end();
        Serial.printf("Loaded learned biases, rewards: %lu\n", g_total_rewards);
    }

    g_buffer_count = 0;
    g_initialized = true;
}

void online_learn_reward(uint8_t reward_action, const Features* features) {
    if (!g_initialized) return;
    if (reward_action >= ACTION_COUNT) return;

    // Add to experience buffer
    if (g_buffer_count < MAX_BUFFER_SIZE) {
        Experience* exp = &g_buffer[g_buffer_count++];
        memcpy(exp->features, &features->hunger, 12 * sizeof(float));
        exp->action = reward_action;
        exp->reward = 1.0f;  // Positive reinforcement
        g_total_rewards++;

        Serial.printf("Online learn: reward action %d, buffer %d\n",
                     reward_action, g_buffer_count);
    }
}

bool online_learn_apply(void) {
    if (!g_initialized || g_buffer_count == 0) return false;

    // Apply decay to all biases (forgetting factor)
    for (int i = 0; i < ACTION_COUNT; i++) {
        g_action_biases[i] *= DECAY_RATE;
    }

    // Learn from experience buffer
    for (uint8_t e = 0; e < g_buffer_count; e++) {
        Experience* exp = &g_buffer[e];

        // Increase bias for rewarded action
        g_action_biases[exp->action] += LEARNING_RATE * exp->reward;

        // Slightly decrease bias for other actions (competition)
        for (int i = 0; i < ACTION_COUNT; i++) {
            if (i != exp->action) {
                g_action_biases[i] -= LEARNING_RATE * exp->reward * 0.1f;
            }
        }
    }

    // Clamp biases to reasonable range
    for (int i = 0; i < ACTION_COUNT; i++) {
        g_action_biases[i] = fmaxf(-2.0f, fminf(2.0f, g_action_biases[i]));
    }

    g_buffer_count = 0;

    Serial.println("Online learn: applied updates");
    return true;
}

void online_learn_save(void) {
    if (!g_initialized) return;

    if (g_prefs.begin("learn", false)) {  // read-write
        for (int i = 0; i < ACTION_COUNT; i++) {
            char key[8];
            snprintf(key, sizeof(key), "b%d", i);
            g_prefs.putFloat(key, g_action_biases[i]);
        }
        g_prefs.putUInt("count", g_total_rewards);
        g_prefs.end();
        Serial.println("Saved learned biases");
    }
}

uint32_t online_learn_get_reward_count(void) {
    return g_total_rewards;
}

// Get the learned bias for an action (called by brain_infer)
float online_learn_get_bias(uint8_t action) {
    if (action >= ACTION_COUNT) return 0.0f;
    return g_action_biases[action];
}
