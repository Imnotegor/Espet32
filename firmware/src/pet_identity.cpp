#include "pet_identity.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>

static PetIdentity g_identity;
static Preferences g_prefs;
static bool g_initialized = false;

// Default pet names based on pattern seed
static const char* DEFAULT_NAMES[] = {
    "Pixel", "Byte", "Chip", "Spark", "Glitch",
    "Neon", "Pulse", "Echo", "Flux", "Nova",
    "Bit", "Core", "Sync", "Volt", "Zen"
};
static const int NUM_DEFAULT_NAMES = 15;

// HSV to RGB conversion helper
static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                       uint8_t* r, uint8_t* g, uint8_t* b) {
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }

    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;

    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

void pet_identity_init(void) {
    if (g_initialized) return;

    // Get MAC address for HWID
    uint8_t mac[6];
    WiFi.macAddress(mac);

    // Format as hex string
    snprintf(g_identity.hwid, sizeof(g_identity.hwid),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Generate unique hues from MAC address
    // Use different parts of MAC to get varied but consistent colors
    uint32_t mac_hash = (mac[0] << 24) | (mac[1] << 16) | (mac[2] << 8) | mac[3];
    mac_hash ^= (mac[4] << 8) | mac[5];

    g_identity.primary_hue = (mac_hash & 0xFF);
    g_identity.secondary_hue = ((mac_hash >> 8) & 0xFF);
    g_identity.pattern_seed = ((mac_hash >> 16) & 0xFF);

    // Make sure secondary hue is different enough from primary
    int hue_diff = abs((int)g_identity.secondary_hue - (int)g_identity.primary_hue);
    if (hue_diff < 40 || hue_diff > 216) {
        g_identity.secondary_hue = (g_identity.primary_hue + 85) % 256; // ~120 degrees apart
    }

    // Load name from NVS
    if (g_prefs.begin("pet_id", true)) {
        String saved_name = g_prefs.getString("name", "");
        g_prefs.end();

        if (saved_name.length() > 0) {
            strncpy(g_identity.name, saved_name.c_str(), PET_NAME_MAX_LEN);
            g_identity.name[PET_NAME_MAX_LEN] = '\0';
        } else {
            // Generate default name from pattern seed
            int name_idx = g_identity.pattern_seed % NUM_DEFAULT_NAMES;
            strncpy(g_identity.name, DEFAULT_NAMES[name_idx], PET_NAME_MAX_LEN);
            g_identity.name[PET_NAME_MAX_LEN] = '\0';
        }
    } else {
        // No saved name, use default
        int name_idx = g_identity.pattern_seed % NUM_DEFAULT_NAMES;
        strncpy(g_identity.name, DEFAULT_NAMES[name_idx], PET_NAME_MAX_LEN);
        g_identity.name[PET_NAME_MAX_LEN] = '\0';
    }

    g_initialized = true;

    Serial.printf("Pet Identity initialized:\n");
    Serial.printf("  HWID: %s\n", g_identity.hwid);
    Serial.printf("  Name: %s\n", g_identity.name);
    Serial.printf("  Primary Hue: %d, Secondary Hue: %d\n",
                  g_identity.primary_hue, g_identity.secondary_hue);
}

const PetIdentity* pet_identity_get(void) {
    return &g_identity;
}

bool pet_identity_set_name(const char* name) {
    if (!name || strlen(name) == 0) return false;

    strncpy(g_identity.name, name, PET_NAME_MAX_LEN);
    g_identity.name[PET_NAME_MAX_LEN] = '\0';

    // Save to NVS
    if (g_prefs.begin("pet_id", false)) {
        g_prefs.putString("name", g_identity.name);
        g_prefs.end();
        Serial.printf("Pet name saved: %s\n", g_identity.name);
        return true;
    }

    return false;
}

const char* pet_identity_get_hwid(void) {
    return g_identity.hwid;
}

const char* pet_identity_get_name(void) {
    return g_identity.name;
}

void pet_identity_get_colors(uint8_t* primary_r, uint8_t* primary_g, uint8_t* primary_b,
                             uint8_t* secondary_r, uint8_t* secondary_g, uint8_t* secondary_b) {
    // Convert HSV to RGB with good saturation and brightness
    hsv_to_rgb(g_identity.primary_hue, 200, 220,
               primary_r, primary_g, primary_b);
    hsv_to_rgb(g_identity.secondary_hue, 180, 200,
               secondary_r, secondary_g, secondary_b);
}

uint8_t pet_identity_get_pattern(void) {
    return g_identity.pattern_seed;
}
