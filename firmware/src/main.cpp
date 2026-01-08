#include <Arduino.h>
#include "core_state.h"
#include "buttons.h"
#include "rgb_renderer.h"
#include "storage.h"
#include "web_server.h"
#include "logger.h"
#include "brain_infer.h"
#include "time_manager.h"
#include "online_learn.h"
#include "embedded_model.h"
#include "sleep_manager.h"
#include "pet_identity.h"

// Pin assignments
#define BUTTON_FEED_PIN     17  // GPIO17 - Column 0 (hunger button)
#define BUTTON_PET_PIN      0   // GPIO0 - Column 1 (pet button)
#define RGB_LED_PIN         21  // GPIO21 - WS2812 data line
#define RGB_POWER_PIN       40  // GPIO40 - WS2812 power control
#define RGB_LED_COUNT       2   // Two LEDs on DualKey board

// WiFi AP settings
#define WIFI_SSID           "NeuroPet"
#define WIFI_PASSWORD       "petpetpet"

// Timing
#define TICK_INTERVAL_MS    2000    // State update every 2 seconds
#define SAVE_INTERVAL_MS    60000   // Save state every minute
#define BUTTON_POLL_MS      10      // Button polling interval

static PetState g_pet_state;
static StateConfig g_state_config;
static InteractionStats g_interaction_stats;
static BrainOutput g_brain_output;
static RGBOutput g_rgb_output;
static Features g_current_features;
static PetStats g_pet_stats;

static uint32_t g_last_tick_ms = 0;
static uint32_t g_last_save_ms = 0;
static uint32_t g_last_button_ms = 0;

static bool g_use_fallback_brain = true;

// Sleep mode tracking
static uint32_t g_both_buttons_start_ms = 0;
static const uint32_t SLEEP_HOLD_TIME_MS = 3000;  // Hold both buttons for 3 seconds

void on_button_event(ButtonEvent event) {
    uint32_t now = millis();
    InputEventType input_type = INPUT_NONE;

    // Flash LED for feedback (LED 0 = hunger/feed, LED 1 = mood/pet)
    RGBColor flash_color;
    uint8_t flash_led = 0;

    if (event.button == BUTTON_FEED) {
        // Feed button pressed -> flash LED 0 (hunger LED)
        flash_led = 0;
        switch (event.gesture) {
            case GESTURE_SHORT:
                input_type = INPUT_FEED_SHORT;
                core_state_feed(&g_pet_state, &g_state_config, &g_interaction_stats);
                g_pet_stats.total_feeds++;
                flash_color = {100, 255, 100}; // Green flash = fed!
                Serial.println("Feed: short");
                break;
            case GESTURE_LONG:
                input_type = INPUT_FEED_LONG;
                core_state_feed(&g_pet_state, &g_state_config, &g_interaction_stats);
                core_state_feed(&g_pet_state, &g_state_config, &g_interaction_stats);
                g_pet_stats.total_feeds += 2;
                flash_color = {150, 255, 150}; // Bright green = double feed
                Serial.println("Feed: long (double portion)");
                break;
            case GESTURE_DOUBLE:
                input_type = INPUT_FEED_DOUBLE;
                flash_color = {255, 255, 100}; // Yellow = special
                Serial.println("Feed: double (special)");
                break;
            default:
                break;
        }
    } else if (event.button == BUTTON_PET) {
        // Pet button pressed -> flash LED 1 (mood LED)
        flash_led = 1;
        switch (event.gesture) {
            case GESTURE_SHORT:
                input_type = INPUT_PET_SHORT;
                core_state_pet(&g_pet_state, &g_state_config, &g_interaction_stats);
                g_pet_stats.total_pets++;
                flash_color = {255, 150, 255}; // Pink flash = pet!
                Serial.println("Pet: short");
                break;
            case GESTURE_LONG:
                input_type = INPUT_PET_LONG;
                core_state_pet(&g_pet_state, &g_state_config, &g_interaction_stats);
                core_state_pet(&g_pet_state, &g_state_config, &g_interaction_stats);
                g_pet_stats.total_pets += 2;
                flash_color = {255, 200, 255}; // Bright pink = extra love
                Serial.println("Pet: long (extra love)");
                break;
            case GESTURE_DOUBLE:
                input_type = INPUT_PET_DOUBLE;
                flash_color = {255, 100, 255}; // Purple = special
                Serial.println("Pet: double (special)");
                break;
            default:
                break;
        }
    }

    if (input_type != INPUT_NONE) {
        // Update interaction time
        g_interaction_stats.last_interaction_ms = now;
        g_interaction_stats.ignore_start_ms = 0;

        // Flash the corresponding LED
        rgb_flash_led(flash_led, &flash_color, 150);

        // Log event
        logger_build_features(&g_current_features, &g_pet_state, &g_interaction_stats, now);
        logger_log_event(input_type, &g_current_features, &g_brain_output, &g_pet_state);

        // Online learning: reinforce appropriate action based on owner's interaction
        if (event.button == BUTTON_FEED) {
            // Owner fed the pet - reinforce ASK_FOOD if pet was asking, or HAPPY
            if (g_brain_output.action_id == ACTION_ASK_FOOD) {
                online_learn_reward(ACTION_ASK_FOOD, &g_current_features);
            }
            online_learn_reward(ACTION_HAPPY, &g_current_features);
        } else if (event.button == BUTTON_PET) {
            // Owner pet the pet - reinforce ASK_PET if pet was asking, or HAPPY
            if (g_brain_output.action_id == ACTION_ASK_PET) {
                online_learn_reward(ACTION_ASK_PET, &g_current_features);
            }
            online_learn_reward(ACTION_HAPPY, &g_current_features);
        }

        // Send to web clients
        const char* event_names[] = {
            "none", "feed_short", "feed_long", "feed_double",
            "pet_short", "pet_long", "pet_double", "ignore"
        };
        web_server_send_event(event_names[input_type], nullptr);
    }
}

void on_model_uploaded(const uint8_t* data, uint32_t size,
                       const ModelMeta* meta, bool success) {
    if (success) {
        Serial.printf("Model uploaded: v%lu, %lu bytes\n", meta->version, size);

        // Try to load the new model
        if (brain_load_weights(data, size)) {
            g_use_fallback_brain = false;
            Serial.println("New brain model activated!");
            web_server_send_event("model_loaded", "success");
        } else {
            Serial.println("Failed to load brain model");
            web_server_send_event("model_loaded", "failed");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== NeuroPet Starting ===");

    // Initialize storage
    if (!storage_init()) {
        Serial.println("Storage init failed!");
    }

    // Initialize state
    core_state_init(&g_pet_state);
    core_state_config_init(&g_state_config);
    core_state_stats_init(&g_interaction_stats);

    // Try to load saved state
    if (storage_load_state(&g_pet_state)) {
        Serial.println("Loaded saved pet state");
    } else {
        Serial.println("Using default pet state");
    }

    // Load stats
    if (storage_load_stats(&g_pet_stats)) {
        g_pet_stats.boot_count++;
    } else {
        memset(&g_pet_stats, 0, sizeof(g_pet_stats));
        g_pet_stats.boot_count = 1;
    }

    // Initialize pet identity (needs WiFi for MAC address)
    pet_identity_init();

    // Initialize time manager
    time_init();

    // Initialize online learning
    online_learn_init();

    // Initialize buttons
    ButtonConfig button_config;
    buttons_config_init(&button_config);
    buttons_init(BUTTON_FEED_PIN, BUTTON_PET_PIN, &button_config);
    buttons_set_callback(on_button_event);

    // Initialize sleep manager
    sleep_init(BUTTON_FEED_PIN, BUTTON_PET_PIN);

    // Check if we woke from sleep
    if (sleep_was_sleeping()) {
        Serial.println("Resumed from deep sleep!");
    }

    // Initialize RGB (dual LEDs with power control for ESP-DualKey)
    rgb_init_dualkey(RGB_LED_PIN, RGB_POWER_PIN, RGB_LED_COUNT);

    // Initialize brain
    brain_init();

    // Try to load custom model from SPIFFS
    if (storage_has_valid_model()) {
        uint8_t* model_buffer = (uint8_t*)malloc(32768);
        if (model_buffer) {
            uint32_t model_size;
            if (storage_load_model(model_buffer, 32768, &model_size)) {
                if (brain_load_weights(model_buffer, model_size)) {
                    g_use_fallback_brain = false;
                    Serial.println("Custom brain model loaded from SPIFFS");
                }
            }
            free(model_buffer);
        }
    }

    // Try embedded model if no SPIFFS model
    if (g_use_fallback_brain && EMBEDDED_MODEL_SIZE > 0) {
        if (brain_load_weights(EMBEDDED_MODEL, EMBEDDED_MODEL_SIZE)) {
            g_use_fallback_brain = false;
            Serial.println("Embedded brain model loaded");
        }
    }

    if (g_use_fallback_brain) {
        Serial.println("Using fallback rule-based brain");
    }

    // Initialize logger
    logger_init();

    // Initialize web server
    if (web_server_init(WIFI_SSID, WIFI_PASSWORD)) {
        web_server_set_model_callback(on_model_uploaded);
        web_server_start();
        Serial.printf("Web server at http://%s\n", web_server_get_ip());
    }

    // Initial brain inference
    g_brain_output.action_id = ACTION_IDLE;
    g_brain_output.valence = 0.0f;
    g_brain_output.arousal = 0.3f;

    g_last_tick_ms = millis();
    g_last_save_ms = millis();
    g_last_button_ms = millis();

    Serial.println("=== NeuroPet Ready ===\n");
}

void loop() {
    uint32_t now = millis();

    // Button polling
    if (now - g_last_button_ms >= BUTTON_POLL_MS) {
        g_last_button_ms = now;
        buttons_update(now);

        // Process any pending button events (if not using callback)
        ButtonEvent event;
        while (buttons_get_event(&event)) {
            on_button_event(event);
        }

        // Check for sleep trigger: both buttons held for 3+ seconds
        bool feed_pressed = buttons_is_pressed(BUTTON_FEED);
        bool pet_pressed = buttons_is_pressed(BUTTON_PET);

        if (feed_pressed && pet_pressed) {
            if (g_both_buttons_start_ms == 0) {
                g_both_buttons_start_ms = now;
            }

            // Show sleep progress indicator (dim blue fading in)
            uint32_t held_time = now - g_both_buttons_start_ms;
            if (held_time > 500) {  // Start showing after 500ms
                float progress = (float)(held_time - 500) / (SLEEP_HOLD_TIME_MS - 500);
                if (progress > 1.0f) progress = 1.0f;
                uint8_t blue = (uint8_t)(progress * 100);
                RGBColor sleep_indicator = {0, 0, blue};
                rgb_set_both_leds(&sleep_indicator, &sleep_indicator);
            }

            if (held_time >= SLEEP_HOLD_TIME_MS) {
                // Both buttons held for 3 seconds - enter sleep mode
                Serial.println("Sleep mode triggered!");

                // Save all state before sleep
                storage_save_state(&g_pet_state);
                storage_save_interaction_stats(&g_interaction_stats);
                g_pet_stats.last_save_timestamp = now / 1000;
                storage_save_stats(&g_pet_stats);
                online_learn_save();

                // Send sleep event to web clients
                web_server_send_event("sleep", "entering_deep_sleep");

                // Enter deep sleep
                sleep_request();
                // Will never return - device resets on wake
            }
        } else {
            g_both_buttons_start_ms = 0;
        }
    }

    // State tick
    if (now - g_last_tick_ms >= TICK_INTERVAL_MS) {
        uint32_t delta = now - g_last_tick_ms;
        g_last_tick_ms = now;

        // Update state simulation
        core_state_update(&g_pet_state, &g_state_config, delta, g_brain_output.action_id);
        core_state_update_stats(&g_interaction_stats, now);

        // Check for ignore (no interaction for 5 minutes)
        if (g_interaction_stats.last_interaction_ms > 0) {
            uint32_t since_interaction = now - g_interaction_stats.last_interaction_ms;
            if (since_interaction > 300000 && g_interaction_stats.ignore_start_ms == 0) {
                g_interaction_stats.ignore_start_ms = now;
                logger_build_features(&g_current_features, &g_pet_state, &g_interaction_stats, now);
                logger_log_event(INPUT_IGNORE, &g_current_features, &g_brain_output, &g_pet_state);
            }
        }

        // Build features and run brain
        logger_build_features(&g_current_features, &g_pet_state, &g_interaction_stats, now);

        if (g_use_fallback_brain) {
            brain_fallback(&g_current_features, &g_brain_output);
        } else {
            brain_infer(&g_current_features, &g_brain_output);
        }

        // Track max trust
        uint32_t trust_scaled = (uint32_t)(g_pet_state.trust * 1000);
        if (trust_scaled > g_pet_stats.max_trust_reached) {
            g_pet_stats.max_trust_reached = trust_scaled;
        }

        // Track starvation
        if (g_pet_state.hunger >= 0.99f) {
            static bool was_starving = false;
            if (!was_starving) {
                g_pet_stats.times_starved++;
                was_starving = true;
            }
        } else {
            static bool was_starving = false;
            was_starving = false;
        }

        // Debug output
        Serial.printf("State: H=%.2f E=%.2f A=%.2f T=%.2f S=%.2f | Action=%d V=%.2f Ar=%.2f\n",
            g_pet_state.hunger, g_pet_state.energy, g_pet_state.affection_need,
            g_pet_state.trust, g_pet_state.stress,
            g_brain_output.action_id, g_brain_output.valence, g_brain_output.arousal);
    }

    // RGB update (smooth animation)
    rgb_update(&g_pet_state, &g_brain_output, now);
    rgb_get_output(&g_rgb_output);

    // Update web server state
    web_server_update_state(&g_pet_state, &g_brain_output, &g_rgb_output);

    // Web server loop
    web_server_loop();

    // Periodic save
    if (now - g_last_save_ms >= SAVE_INTERVAL_MS) {
        g_last_save_ms = now;
        storage_save_state(&g_pet_state);
        storage_save_interaction_stats(&g_interaction_stats);
        g_pet_stats.last_save_timestamp = now / 1000;
        storage_save_stats(&g_pet_stats);

        // Apply and save online learning updates
        online_learn_apply();
        online_learn_save();

        Serial.println("State saved");
    }

    // Small delay to prevent watchdog
    delay(1);
}
