#include "sleep_manager.h"
#include "storage.h"
#include "rgb_renderer.h"
#include "core_state.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <Preferences.h>

static uint8_t g_wake_pin1 = 0;
static uint8_t g_wake_pin2 = 0;
static bool g_was_sleeping = false;
static uint32_t g_sleep_duration = 0;

static Preferences g_prefs;

void sleep_init(uint8_t wakeup_pin1, uint8_t wakeup_pin2) {
    g_wake_pin1 = wakeup_pin1;
    g_wake_pin2 = wakeup_pin2;

    // Check if we woke from deep sleep
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 ||
        wakeup_reason == ESP_SLEEP_WAKEUP_EXT1 ||
        wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
        g_was_sleeping = true;

        // Calculate sleep duration
        if (g_prefs.begin("sleep", true)) {
            uint32_t sleep_start = g_prefs.getUInt("start", 0);
            g_prefs.end();

            if (sleep_start > 0) {
                // We stored Unix-like timestamp approximation
                // Since we don't have RTC, use millis overflow estimation
                g_sleep_duration = 0; // Can't accurately measure without RTC
            }
        }

        Serial.println("Woke up from deep sleep!");
    }
}

void sleep_request(void) {
    Serial.println("Entering deep sleep mode...");

    // Save sleep start time
    if (g_prefs.begin("sleep", false)) {
        g_prefs.putUInt("start", millis() / 1000);
        g_prefs.end();
    }

    // Show sleep animation on LEDs (fade to dim blue)
    for (int i = 255; i >= 0; i -= 5) {
        RGBColor sleep_color = {0, 0, (uint8_t)(i / 4)};  // Dim blue
        rgb_set_both_leds(&sleep_color, &sleep_color);
        delay(20);
    }

    // Turn off LEDs and power
    rgb_power_off();

    delay(100);

    // Configure wake up sources (both buttons)
    // Use EXT1 with multiple GPIO pins - wake on LOW
    uint64_t wake_mask = (1ULL << g_wake_pin1) | (1ULL << g_wake_pin2);
    esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);

    Serial.println("Going to sleep... Press any button to wake up.");
    Serial.flush();

    // Enter deep sleep
    esp_deep_sleep_start();

    // Will never reach here - device resets on wake
}

bool sleep_was_sleeping(void) {
    return g_was_sleeping;
}

uint32_t sleep_get_duration(void) {
    return g_sleep_duration;
}
