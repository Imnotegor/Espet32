#include "storage.h"
#include <Preferences.h>
#include <SPIFFS.h>

static Preferences g_prefs;
static const char* g_last_error = nullptr;
static bool g_initialized = false;

// Namespace keys
static const char* NS_PET = "pet";
static const char* NS_MODEL = "model";

// CRC32 lookup table
static uint32_t crc32_table[256];
static bool crc_table_initialized = false;

static void init_crc32_table() {
    if (crc_table_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc_table_initialized = true;
}

uint32_t storage_calc_crc32(const uint8_t* data, uint32_t length) {
    init_crc32_table();

    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

bool storage_init(void) {
    if (g_initialized) return true;

    init_crc32_table();

    // Initialize SPIFFS for model storage
    if (!SPIFFS.begin(true)) {
        g_last_error = "SPIFFS mount failed";
        return false;
    }

    g_initialized = true;
    g_last_error = nullptr;
    return true;
}

bool storage_save_state(const PetState* state) {
    if (!g_initialized || !state) {
        g_last_error = "Not initialized or invalid state";
        return false;
    }

    g_prefs.begin(NS_PET, false);

    g_prefs.putFloat("hunger", state->hunger);
    g_prefs.putFloat("energy", state->energy);
    g_prefs.putFloat("affection", state->affection_need);
    g_prefs.putFloat("trust", state->trust);
    g_prefs.putFloat("stress", state->stress);

    g_prefs.end();
    return true;
}

bool storage_load_state(PetState* state) {
    if (!g_initialized || !state) {
        g_last_error = "Not initialized or invalid state";
        return false;
    }

    g_prefs.begin(NS_PET, true);

    // Check if state exists
    if (!g_prefs.isKey("hunger")) {
        g_prefs.end();
        g_last_error = "No saved state found";
        return false;
    }

    state->hunger = g_prefs.getFloat("hunger", 0.3f);
    state->energy = g_prefs.getFloat("energy", 0.7f);
    state->affection_need = g_prefs.getFloat("affection", 0.4f);
    state->trust = g_prefs.getFloat("trust", 0.5f);
    state->stress = g_prefs.getFloat("stress", 0.2f);

    g_prefs.end();
    return true;
}

bool storage_save_interaction_stats(const InteractionStats* stats) {
    if (!g_initialized || !stats) {
        g_last_error = "Not initialized or invalid stats";
        return false;
    }

    g_prefs.begin(NS_PET, false);

    g_prefs.putUInt("last_int_ms", stats->last_interaction_ms);
    g_prefs.putUShort("feed_1m", stats->feed_count_1m);
    g_prefs.putUShort("feed_5m", stats->feed_count_5m);
    g_prefs.putUShort("pet_1m", stats->pet_count_1m);
    g_prefs.putUShort("pet_5m", stats->pet_count_5m);
    g_prefs.putFloat("spam", stats->spam_score);

    g_prefs.end();
    return true;
}

bool storage_load_interaction_stats(InteractionStats* stats) {
    if (!g_initialized || !stats) {
        g_last_error = "Not initialized or invalid stats";
        return false;
    }

    g_prefs.begin(NS_PET, true);

    stats->last_interaction_ms = g_prefs.getUInt("last_int_ms", 0);
    stats->feed_count_1m = g_prefs.getUShort("feed_1m", 0);
    stats->feed_count_5m = g_prefs.getUShort("feed_5m", 0);
    stats->pet_count_1m = g_prefs.getUShort("pet_1m", 0);
    stats->pet_count_5m = g_prefs.getUShort("pet_5m", 0);
    stats->spam_score = g_prefs.getFloat("spam", 0.0f);
    stats->ignore_start_ms = 0;

    g_prefs.end();
    return true;
}

bool storage_save_stats(const PetStats* stats) {
    if (!g_initialized || !stats) {
        g_last_error = "Not initialized or invalid stats";
        return false;
    }

    g_prefs.begin(NS_PET, false);

    g_prefs.putUInt("total_feeds", stats->total_feeds);
    g_prefs.putUInt("total_pets", stats->total_pets);
    g_prefs.putUInt("playtime", stats->total_playtime_sec);
    g_prefs.putUInt("max_trust", stats->max_trust_reached);
    g_prefs.putUInt("starved", stats->times_starved);
    g_prefs.putUInt("boots", stats->boot_count);
    g_prefs.putUInt("last_save", stats->last_save_timestamp);

    g_prefs.end();
    return true;
}

bool storage_load_stats(PetStats* stats) {
    if (!g_initialized || !stats) {
        g_last_error = "Not initialized or invalid stats";
        return false;
    }

    g_prefs.begin(NS_PET, true);

    stats->total_feeds = g_prefs.getUInt("total_feeds", 0);
    stats->total_pets = g_prefs.getUInt("total_pets", 0);
    stats->total_playtime_sec = g_prefs.getUInt("playtime", 0);
    stats->max_trust_reached = g_prefs.getUInt("max_trust", 0);
    stats->times_starved = g_prefs.getUInt("starved", 0);
    stats->boot_count = g_prefs.getUInt("boots", 0);
    stats->last_save_timestamp = g_prefs.getUInt("last_save", 0);

    g_prefs.end();
    return true;
}

bool storage_save_model(const uint8_t* model_data, uint32_t size,
                        const ModelMeta* meta) {
    if (!g_initialized || !model_data || !meta) {
        g_last_error = "Invalid parameters";
        return false;
    }

    // Verify CRC
    uint32_t calc_crc = storage_calc_crc32(model_data, size);
    if (calc_crc != meta->crc32) {
        g_last_error = "CRC mismatch";
        return false;
    }

    // Save model blob to SPIFFS
    File file = SPIFFS.open("/model.bin", "w");
    if (!file) {
        g_last_error = "Failed to open model file";
        return false;
    }

    size_t written = file.write(model_data, size);
    file.close();

    if (written != size) {
        g_last_error = "Write incomplete";
        return false;
    }

    // Save metadata to NVS
    g_prefs.begin(NS_MODEL, false);

    g_prefs.putUInt("version", meta->version);
    g_prefs.putUInt("feat_ver", meta->features_version);
    g_prefs.putUInt("size", meta->size);
    g_prefs.putUInt("crc32", meta->crc32);
    g_prefs.putUInt("created", meta->created_at);
    g_prefs.putBool("valid", true);

    g_prefs.end();
    return true;
}

bool storage_load_model(uint8_t* model_data, uint32_t max_size,
                        uint32_t* actual_size) {
    if (!g_initialized || !model_data) {
        g_last_error = "Invalid parameters";
        return false;
    }

    // Load metadata first
    ModelMeta meta;
    if (!storage_load_model_meta(&meta)) {
        return false;
    }

    if (meta.size > max_size) {
        g_last_error = "Buffer too small";
        return false;
    }

    // Load model from SPIFFS
    File file = SPIFFS.open("/model.bin", "r");
    if (!file) {
        g_last_error = "Model file not found";
        return false;
    }

    size_t read = file.read(model_data, meta.size);
    file.close();

    if (read != meta.size) {
        g_last_error = "Read incomplete";
        return false;
    }

    // Verify CRC
    uint32_t calc_crc = storage_calc_crc32(model_data, meta.size);
    if (calc_crc != meta.crc32) {
        g_last_error = "CRC verification failed";
        return false;
    }

    if (actual_size) {
        *actual_size = meta.size;
    }

    return true;
}

bool storage_load_model_meta(ModelMeta* meta) {
    if (!g_initialized || !meta) {
        g_last_error = "Invalid parameters";
        return false;
    }

    g_prefs.begin(NS_MODEL, true);

    if (!g_prefs.getBool("valid", false)) {
        g_prefs.end();
        g_last_error = "No valid model";
        return false;
    }

    meta->version = g_prefs.getUInt("version", 0);
    meta->features_version = g_prefs.getUInt("feat_ver", 0);
    meta->size = g_prefs.getUInt("size", 0);
    meta->crc32 = g_prefs.getUInt("crc32", 0);
    meta->created_at = g_prefs.getUInt("created", 0);

    g_prefs.end();
    return true;
}

bool storage_has_valid_model(void) {
    if (!g_initialized) return false;

    g_prefs.begin(NS_MODEL, true);
    bool valid = g_prefs.getBool("valid", false);
    g_prefs.end();

    return valid;
}

bool storage_use_fallback_model(void) {
    return !storage_has_valid_model();
}

bool storage_reset_all(void) {
    if (!g_initialized) return false;

    // Clear pet namespace
    g_prefs.begin(NS_PET, false);
    g_prefs.clear();
    g_prefs.end();

    // Clear model namespace
    g_prefs.begin(NS_MODEL, false);
    g_prefs.clear();
    g_prefs.end();

    // Remove model file
    SPIFFS.remove("/model.bin");

    return true;
}

const char* storage_get_last_error(void) {
    return g_last_error;
}
