#include "brain_infer.h"
#include "online_learn.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static BrainWeights g_weights;
static bool g_custom_model_loaded = false;
static uint32_t g_model_version = 0;

// Activation functions
static float relu(float x) {
    return x > 0 ? x : 0;
}

static float sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

static float tanh_f(float x) {
    return tanhf(x);
}

// Softmax for action probabilities
static void softmax(float* input, int size) {
    float max_val = input[0];
    for (int i = 1; i < size; i++) {
        if (input[i] > max_val) max_val = input[i];
    }

    float sum = 0;
    for (int i = 0; i < size; i++) {
        input[i] = expf(input[i] - max_val);
        sum += input[i];
    }

    for (int i = 0; i < size; i++) {
        input[i] /= sum;
    }
}

// Initialize with random (but deterministic) fallback weights
static void init_fallback_weights(void) {
    // Simple pseudo-random initialization based on indices
    // This provides a baseline "personality"
    for (int i = 0; i < BRAIN_INPUT_SIZE; i++) {
        for (int j = 0; j < BRAIN_HIDDEN_SIZE; j++) {
            // Xavier-like initialization
            float scale = 1.0f / sqrtf((float)BRAIN_INPUT_SIZE);
            g_weights.w1[i][j] = ((float)((i * 17 + j * 31) % 100) / 50.0f - 1.0f) * scale;
        }
    }

    for (int j = 0; j < BRAIN_HIDDEN_SIZE; j++) {
        g_weights.b1[j] = 0.0f;
    }

    for (int i = 0; i < BRAIN_HIDDEN_SIZE; i++) {
        for (int j = 0; j < BRAIN_OUTPUT_SIZE; j++) {
            float scale = 1.0f / sqrtf((float)BRAIN_HIDDEN_SIZE);
            g_weights.w2[i][j] = ((float)((i * 23 + j * 41) % 100) / 50.0f - 1.0f) * scale;
        }
    }

    for (int j = 0; j < BRAIN_OUTPUT_SIZE; j++) {
        g_weights.b2[j] = 0.0f;
    }

    // Bias the output towards reasonable behaviors
    // Make "idle" more likely initially
    g_weights.b2[ACTION_IDLE] = 0.5f;

    // Valence starts neutral
    g_weights.b2[BRAIN_ACTION_COUNT] = 0.0f;

    // Arousal starts low
    g_weights.b2[BRAIN_ACTION_COUNT + 1] = -0.5f;
}

void brain_init(void) {
    init_fallback_weights();
    g_custom_model_loaded = false;
    g_model_version = 0;
}

bool brain_load_weights(const uint8_t* data, uint32_t size) {
    // Expected size: all weights as float32
    uint32_t expected_size =
        (BRAIN_INPUT_SIZE * BRAIN_HIDDEN_SIZE +     // w1
         BRAIN_HIDDEN_SIZE +                         // b1
         BRAIN_HIDDEN_SIZE * BRAIN_OUTPUT_SIZE +    // w2
         BRAIN_OUTPUT_SIZE) * sizeof(float);        // b2

    if (size < expected_size + sizeof(uint32_t)) {
        return false;
    }

    // First 4 bytes are version
    memcpy(&g_model_version, data, sizeof(uint32_t));
    data += sizeof(uint32_t);

    // Load weights in order
    const float* fdata = (const float*)data;
    int idx = 0;

    // w1
    for (int i = 0; i < BRAIN_INPUT_SIZE; i++) {
        for (int j = 0; j < BRAIN_HIDDEN_SIZE; j++) {
            g_weights.w1[i][j] = fdata[idx++];
        }
    }

    // b1
    for (int j = 0; j < BRAIN_HIDDEN_SIZE; j++) {
        g_weights.b1[j] = fdata[idx++];
    }

    // w2
    for (int i = 0; i < BRAIN_HIDDEN_SIZE; i++) {
        for (int j = 0; j < BRAIN_OUTPUT_SIZE; j++) {
            g_weights.w2[i][j] = fdata[idx++];
        }
    }

    // b2
    for (int j = 0; j < BRAIN_OUTPUT_SIZE; j++) {
        g_weights.b2[j] = fdata[idx++];
    }

    g_custom_model_loaded = true;
    return true;
}

bool brain_has_custom_model(void) {
    return g_custom_model_loaded;
}

void brain_infer_raw(const float* input, float* output) {
    // Hidden layer
    float hidden[BRAIN_HIDDEN_SIZE];

    for (int j = 0; j < BRAIN_HIDDEN_SIZE; j++) {
        hidden[j] = g_weights.b1[j];
        for (int i = 0; i < BRAIN_INPUT_SIZE; i++) {
            hidden[j] += input[i] * g_weights.w1[i][j];
        }
        hidden[j] = relu(hidden[j]);
    }

    // Output layer
    for (int j = 0; j < BRAIN_OUTPUT_SIZE; j++) {
        output[j] = g_weights.b2[j];
        for (int i = 0; i < BRAIN_HIDDEN_SIZE; i++) {
            output[j] += hidden[i] * g_weights.w2[i][j];
        }
    }

    // Apply activation to emotions (last 2 outputs)
    // Valence: tanh -> [-1, 1]
    output[BRAIN_ACTION_COUNT] = tanh_f(output[BRAIN_ACTION_COUNT]);
    // Arousal: sigmoid -> [0, 1]
    output[BRAIN_ACTION_COUNT + 1] = sigmoid(output[BRAIN_ACTION_COUNT + 1]);

    // Apply softmax to actions
    softmax(output, BRAIN_ACTION_COUNT);
}

void brain_infer(const Features* features, BrainOutput* output) {
    if (!features || !output) return;

    // Convert Features struct to float array
    float input[BRAIN_INPUT_SIZE] = {
        features->hunger,
        features->energy,
        features->affection_need,
        features->trust,
        features->stress,
        features->dt_seconds_norm,
        features->feed_count_5m_norm,
        features->pet_count_5m_norm,
        features->ignore_time_norm,
        features->time_of_day_sin,
        features->time_of_day_cos,
        features->spam_score_norm
    };

    float raw_output[BRAIN_OUTPUT_SIZE];
    brain_infer_raw(input, raw_output);

    // Add learned biases from online learning
    for (int i = 0; i < BRAIN_ACTION_COUNT; i++) {
        raw_output[i] += online_learn_get_bias(i);
    }

    // Find best action
    int best_action = 0;
    float best_prob = raw_output[0];
    for (int i = 1; i < BRAIN_ACTION_COUNT; i++) {
        if (raw_output[i] > best_prob) {
            best_prob = raw_output[i];
            best_action = i;
        }
    }

    output->action_id = (PetAction)best_action;
    output->valence = raw_output[BRAIN_ACTION_COUNT];
    output->arousal = raw_output[BRAIN_ACTION_COUNT + 1];
}

void brain_fallback(const Features* features, BrainOutput* output) {
    if (!features || !output) return;

    // Simple rule-based fallback
    float hunger = features->hunger;
    float energy = features->energy;
    float affection = features->affection_need;
    float stress = features->stress;

    // Default
    output->action_id = ACTION_IDLE;
    output->valence = 0.0f;
    output->arousal = 0.3f;

    // Priority: sleep > food > affection > play > idle
    if (energy < 0.2f) {
        output->action_id = ACTION_SLEEP;
        output->valence = 0.0f;
        output->arousal = 0.1f;
    } else if (hunger > 0.7f) {
        output->action_id = ACTION_ASK_FOOD;
        output->valence = -0.3f;
        output->arousal = 0.5f + hunger * 0.3f;
    } else if (affection > 0.6f) {
        output->action_id = ACTION_ASK_PET;
        output->valence = -0.1f;
        output->arousal = 0.4f;
    } else if (stress > 0.6f) {
        output->action_id = ACTION_ANNOYED;
        output->valence = -0.5f;
        output->arousal = 0.6f;
    } else if (hunger < 0.3f && energy > 0.5f && affection < 0.3f) {
        // Well-fed, rested, and loved
        if (features->dt_seconds_norm > 0.5f) {
            output->action_id = ACTION_SAD;
            output->valence = -0.2f;
            output->arousal = 0.2f;
        } else {
            output->action_id = ACTION_HAPPY;
            output->valence = 0.7f;
            output->arousal = 0.5f;
        }
    } else if (energy > 0.6f && stress < 0.3f) {
        output->action_id = ACTION_PLAY;
        output->valence = 0.4f;
        output->arousal = 0.6f;
    }

    // Adjust valence based on trust
    output->valence += (features->trust - 0.5f) * 0.3f;

    // Clamp
    if (output->valence < -1.0f) output->valence = -1.0f;
    if (output->valence > 1.0f) output->valence = 1.0f;
    if (output->arousal < 0.0f) output->arousal = 0.0f;
    if (output->arousal > 1.0f) output->arousal = 1.0f;
}

void brain_reset(void) {
    init_fallback_weights();
    g_custom_model_loaded = false;
    g_model_version = 0;
}

uint32_t brain_get_model_version(void) {
    return g_model_version;
}

bool brain_is_quantized(void) {
    return false; // Current implementation uses float32
}
