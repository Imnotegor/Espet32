#include "time_manager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

static Preferences g_prefs;
static uint32_t g_boot_time_ms = 0;      // millis() at boot
static uint32_t g_time_offset_ms = 0;    // Offset to add to get real time (ms since midnight)
static bool g_time_set = false;

void time_init(void) {
    g_boot_time_ms = millis();

    // Load saved time offset from NVS
    if (g_prefs.begin("time", true)) {  // read-only
        g_time_offset_ms = g_prefs.getUInt("offset", 0);
        g_time_set = g_prefs.getBool("set", false);
        g_prefs.end();

        if (g_time_set) {
            Serial.printf("Loaded time offset: %lu ms\n", g_time_offset_ms);
        }
    }
}

void time_set(uint8_t hour, uint8_t minute) {
    // Calculate ms since midnight for the given time
    uint32_t target_ms = (uint32_t)hour * 3600000UL + (uint32_t)minute * 60000UL;

    // Current "device time" ms since midnight (wrapping every 24h)
    uint32_t elapsed_ms = millis() - g_boot_time_ms;
    uint32_t device_time_ms = elapsed_ms % 86400000UL;

    // Calculate offset needed
    if (target_ms >= device_time_ms) {
        g_time_offset_ms = target_ms - device_time_ms;
    } else {
        // Wrap around midnight
        g_time_offset_ms = 86400000UL - device_time_ms + target_ms;
    }

    g_time_set = true;

    Serial.printf("Time set to %02d:%02d (offset: %lu ms)\n", hour, minute, g_time_offset_ms);

    time_save();
}

void time_get(uint8_t* hour, uint8_t* minute) {
    uint32_t elapsed_ms = millis() - g_boot_time_ms;
    uint32_t current_ms = (elapsed_ms + g_time_offset_ms) % 86400000UL;

    uint32_t total_minutes = current_ms / 60000UL;
    *hour = (total_minutes / 60) % 24;
    *minute = total_minutes % 60;
}

void time_get_features(float* sin_out, float* cos_out) {
    uint32_t elapsed_ms = millis() - g_boot_time_ms;
    uint32_t current_ms = (elapsed_ms + g_time_offset_ms) % 86400000UL;

    // Convert to angle (0 = midnight, PI = noon, 2*PI = midnight again)
    float hour_angle = (float)current_ms / 86400000.0f * 2.0f * M_PI;

    *sin_out = sinf(hour_angle);
    *cos_out = cosf(hour_angle);
}

bool time_is_night(void) {
    uint8_t hour, minute;
    time_get(&hour, &minute);

    // Night: 22:00 - 07:00
    return (hour >= 22 || hour < 7);
}

void time_save(void) {
    if (g_prefs.begin("time", false)) {  // read-write
        g_prefs.putUInt("offset", g_time_offset_ms);
        g_prefs.putBool("set", g_time_set);
        g_prefs.end();
        Serial.println("Time offset saved");
    }
}
