#ifndef BRAIN_INFER_H
#define BRAIN_INFER_H

#include <stdint.h>
#include <stdbool.h>
#include "core_state.h"
#include "logger.h"

// Model architecture constants
#define BRAIN_INPUT_SIZE    12      // Number of input features
#define BRAIN_HIDDEN_SIZE   16      // Hidden layer size
#define BRAIN_ACTION_COUNT  8       // Number of actions
#define BRAIN_OUTPUT_SIZE   10      // Actions (8) + valence (1) + arousal (1)

// Model weights structure (for custom MLP)
typedef struct {
    float w1[BRAIN_INPUT_SIZE][BRAIN_HIDDEN_SIZE];      // Input -> Hidden
    float b1[BRAIN_HIDDEN_SIZE];                         // Hidden bias
    float w2[BRAIN_HIDDEN_SIZE][BRAIN_OUTPUT_SIZE];     // Hidden -> Output
    float b2[BRAIN_OUTPUT_SIZE];                         // Output bias
} BrainWeights;

// Initialize brain with fallback (rule-based) model
void brain_init(void);

// Load trained weights from buffer
// Returns true if successful
bool brain_load_weights(const uint8_t* data, uint32_t size);

// Check if custom model is loaded
bool brain_has_custom_model(void);

// Run inference
// features: array of BRAIN_INPUT_SIZE floats
// output: filled with action_id, valence, arousal
void brain_infer(const Features* features, BrainOutput* output);

// Run inference with raw float array
void brain_infer_raw(const float* input, float* output);

// Get fallback (rule-based) decision
void brain_fallback(const Features* features, BrainOutput* output);

// Reset to fallback model
void brain_reset(void);

// Get model info
uint32_t brain_get_model_version(void);
bool brain_is_quantized(void);

#endif // BRAIN_INFER_H
