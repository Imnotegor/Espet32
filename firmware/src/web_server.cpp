#include "web_server.h"
#include "web_content.h"
#include "time_manager.h"
#include "pet_identity.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

static WebServer g_http_server(80);
static WebSocketsServer g_ws_server(81);

static bool g_initialized = false;
static bool g_running = false;
static char g_ip_address[16] = "0.0.0.0";

// Current state for broadcasting
static PetState g_current_state;
static BrainOutput g_current_brain;
static RGBOutput g_current_rgb;
static uint32_t g_last_broadcast_ms = 0;

static ModelUploadCallback g_model_callback = nullptr;
static bool g_uploading = false;

// Model upload buffer
static uint8_t* g_upload_buffer = nullptr;
static uint32_t g_upload_size = 0;
static uint32_t g_upload_received = 0;
static ModelMeta g_upload_meta;

// HTML content from web_content.cpp

// Forward declarations
static void handle_root();
static void handle_api_status();
static void handle_api_log();
static void handle_api_model_get();
static void handle_api_model_post();
static void handle_api_model_meta();
static void handle_api_time_get();
static void handle_api_time_post();
static void handle_api_pet_get();
static void handle_api_pet_name_post();
static void handle_not_found();
static void ws_event(uint8_t num, WStype_t type, uint8_t* payload, size_t length);

bool web_server_init(const char* ssid, const char* password) {
    if (g_initialized) return true;

    const char* ap_ssid = ssid ? ssid : "NeuroPet";
    const char* ap_pass = password ? password : "petpetpet";

    // Configure WiFi for stability
    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_AP);

    // Set TX power to maximum for better range
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // Disable WiFi sleep for stable connection
    WiFi.setSleep(false);

    // Start AP with specific channel (6 is usually less congested)
    // max_connection=4, ssid_hidden=false, channel=6
    WiFi.softAP(ap_ssid, ap_pass, 6, false, 4);

    delay(500);  // Wait for AP to stabilize

    // Configure AP IP settings
    IPAddress local_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_ip, gateway, subnet);

    IPAddress ip = WiFi.softAPIP();
    snprintf(g_ip_address, sizeof(g_ip_address), "%d.%d.%d.%d",
             ip[0], ip[1], ip[2], ip[3]);

    Serial.print("AP started. IP: ");
    Serial.println(g_ip_address);
    Serial.printf("Channel: 6, TX Power: 19.5dBm\n");

    // Setup HTTP routes
    g_http_server.on("/", HTTP_GET, handle_root);
    g_http_server.on("/api/status", HTTP_GET, handle_api_status);
    g_http_server.on("/api/log", HTTP_GET, handle_api_log);
    g_http_server.on("/api/model", HTTP_GET, handle_api_model_get);
    g_http_server.on("/api/model", HTTP_POST, handle_api_model_post);
    g_http_server.on("/api/model/meta", HTTP_GET, handle_api_model_meta);
    g_http_server.on("/api/time", HTTP_GET, handle_api_time_get);
    g_http_server.on("/api/time", HTTP_POST, handle_api_time_post);
    g_http_server.on("/api/pet", HTTP_GET, handle_api_pet_get);
    g_http_server.on("/api/pet/name", HTTP_POST, handle_api_pet_name_post);
    g_http_server.onNotFound(handle_not_found);

    // Setup WebSocket
    g_ws_server.onEvent(ws_event);

    // Initialize state
    core_state_init(&g_current_state);
    g_current_brain.action_id = ACTION_IDLE;
    g_current_brain.valence = 0.0f;
    g_current_brain.arousal = 0.3f;

    g_initialized = true;
    return true;
}

void web_server_start(void) {
    if (!g_initialized || g_running) return;

    g_http_server.begin();
    g_ws_server.begin();

    // WebSocket stability settings
    g_ws_server.enableHeartbeat(15000, 3000, 2);  // ping every 15s, timeout 3s, 2 retries

    g_running = true;

    Serial.println("Web server started");
}

void web_server_stop(void) {
    if (!g_running) return;

    g_http_server.stop();
    g_ws_server.close();
    g_running = false;
}

void web_server_loop(void) {
    if (!g_running) return;

    g_http_server.handleClient();
    g_ws_server.loop();

    // Broadcast state periodically (every 300ms)
    uint32_t now = millis();
    if (now - g_last_broadcast_ms >= 300) {
        g_last_broadcast_ms = now;

        // Build state JSON
        StaticJsonDocument<512> doc;
        doc["type"] = "state_update";
        doc["ts"] = now / 1000;

        JsonObject state = doc.createNestedObject("state");
        state["hunger"] = g_current_state.hunger;
        state["energy"] = g_current_state.energy;
        state["affection"] = g_current_state.affection_need;
        state["trust"] = g_current_state.trust;
        state["stress"] = g_current_state.stress;

        JsonObject brain = doc.createNestedObject("brain");
        brain["action_id"] = (int)g_current_brain.action_id;
        brain["valence"] = g_current_brain.valence;
        brain["arousal"] = g_current_brain.arousal;

        JsonObject rgb = doc.createNestedObject("rgb");
        rgb["r"] = g_current_rgb.color.r;
        rgb["g"] = g_current_rgb.color.g;
        rgb["b"] = g_current_rgb.color.b;
        rgb["pulse"] = g_current_rgb.pulse;

        String output;
        serializeJson(doc, output);
        g_ws_server.broadcastTXT(output);
    }
}

void web_server_update_state(const PetState* state, const BrainOutput* brain,
                             const RGBOutput* rgb) {
    if (state) g_current_state = *state;
    if (brain) g_current_brain = *brain;
    if (rgb) g_current_rgb = *rgb;
}

void web_server_send_event(const char* event_type, const char* data) {
    if (!g_running) return;

    StaticJsonDocument<256> doc;
    doc["type"] = "event";
    doc["ts"] = millis() / 1000;
    doc["event"] = event_type;
    if (data) doc["data"] = data;

    String output;
    serializeJson(doc, output);
    g_ws_server.broadcastTXT(output);
}

uint8_t web_server_get_client_count(void) {
    return g_ws_server.connectedClients();
}

bool web_server_is_uploading(void) {
    return g_uploading;
}

const char* web_server_get_ip(void) {
    return g_ip_address;
}

void web_server_set_model_callback(ModelUploadCallback callback) {
    g_model_callback = callback;
}

// HTTP Handlers
static void handle_root() {
    // Serve from SPIFFS if available
    if (SPIFFS.exists("/index.html")) {
        File file = SPIFFS.open("/index.html", "r");
        g_http_server.streamFile(file, "text/html");
        file.close();
    } else {
        // Use embedded HTML
        g_http_server.send_P(200, "text/html", index_html);
    }
}

static void handle_api_status() {
    StaticJsonDocument<512> doc;

    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["features_version"] = FEATURES_SCHEMA_VERSION;
    doc["uptime"] = millis() / 1000;
    doc["clients"] = g_ws_server.connectedClients();

    JsonObject state = doc.createNestedObject("state");
    state["hunger"] = g_current_state.hunger;
    state["energy"] = g_current_state.energy;
    state["affection"] = g_current_state.affection_need;
    state["trust"] = g_current_state.trust;
    state["stress"] = g_current_state.stress;

    JsonObject brain = doc.createNestedObject("brain");
    brain["action_id"] = (int)g_current_brain.action_id;
    brain["valence"] = g_current_brain.valence;
    brain["arousal"] = g_current_brain.arousal;

    String output;
    serializeJson(doc, output);
    g_http_server.send(200, "application/json", output);
}

static void handle_api_log() {
    // TODO: Implement log retrieval from logger module
    g_http_server.send(200, "application/json", "[]");
}

static void handle_api_model_get() {
    if (!storage_has_valid_model()) {
        g_http_server.send(404, "application/json", "{\"error\":\"No model\"}");
        return;
    }

    ModelMeta meta;
    storage_load_model_meta(&meta);

    File file = SPIFFS.open("/model.bin", "r");
    if (!file) {
        g_http_server.send(500, "application/json", "{\"error\":\"Read failed\"}");
        return;
    }

    g_http_server.sendHeader("Content-Disposition", "attachment; filename=model.bin");
    g_http_server.streamFile(file, "application/octet-stream");
    file.close();
}

static void handle_api_model_post() {
    // Handle model upload
    HTTPUpload& upload = g_http_server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        g_uploading = true;
        g_upload_received = 0;

        // Parse expected size from header
        if (g_http_server.hasHeader("X-Model-Size")) {
            g_upload_size = g_http_server.header("X-Model-Size").toInt();
        } else {
            g_upload_size = 32768; // Default max
        }

        // Allocate buffer
        g_upload_buffer = (uint8_t*)malloc(g_upload_size);
        if (!g_upload_buffer) {
            g_http_server.send(500, "application/json", "{\"error\":\"Memory\"}");
            g_uploading = false;
            return;
        }

        // Parse metadata from headers
        g_upload_meta.version = g_http_server.header("X-Model-Version").toInt();
        g_upload_meta.features_version = g_http_server.header("X-Features-Version").toInt();
        g_upload_meta.crc32 = strtoul(g_http_server.header("X-Model-CRC").c_str(), NULL, 16);
        g_upload_meta.created_at = g_http_server.header("X-Model-Created").toInt();

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (g_upload_buffer && (g_upload_received + upload.currentSize) <= g_upload_size) {
            memcpy(g_upload_buffer + g_upload_received, upload.buf, upload.currentSize);
            g_upload_received += upload.currentSize;
        }

    } else if (upload.status == UPLOAD_FILE_END) {
        g_upload_meta.size = g_upload_received;

        bool success = false;
        if (g_upload_buffer) {
            // Verify and save
            success = storage_save_model(g_upload_buffer, g_upload_received, &g_upload_meta);

            // Notify callback
            if (g_model_callback) {
                g_model_callback(g_upload_buffer, g_upload_received, &g_upload_meta, success);
            }

            free(g_upload_buffer);
            g_upload_buffer = nullptr;
        }

        g_uploading = false;

        if (success) {
            g_http_server.send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            g_http_server.send(400, "application/json", "{\"error\":\"Validation failed\"}");
        }
    }
}

static void handle_api_model_meta() {
    ModelMeta meta;
    if (!storage_load_model_meta(&meta)) {
        g_http_server.send(404, "application/json", "{\"error\":\"No model\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    doc["version"] = meta.version;
    doc["features_version"] = meta.features_version;
    doc["size"] = meta.size;
    doc["crc32"] = meta.crc32;
    doc["created_at"] = meta.created_at;

    String output;
    serializeJson(doc, output);
    g_http_server.send(200, "application/json", output);
}

static void handle_api_time_get() {
    uint8_t hour, minute;
    time_get(&hour, &minute);

    StaticJsonDocument<128> doc;
    doc["hour"] = hour;
    doc["minute"] = minute;
    doc["is_night"] = time_is_night();

    String output;
    serializeJson(doc, output);
    g_http_server.send(200, "application/json", output);
}

static void handle_api_time_post() {
    if (!g_http_server.hasArg("plain")) {
        g_http_server.send(400, "application/json", "{\"error\":\"No body\"}");
        return;
    }

    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, g_http_server.arg("plain"));

    if (error) {
        g_http_server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    int hour = doc["hour"] | -1;
    int minute = doc["minute"] | 0;

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        g_http_server.send(400, "application/json", "{\"error\":\"Invalid time\"}");
        return;
    }

    time_set((uint8_t)hour, (uint8_t)minute);

    g_http_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handle_api_pet_get() {
    const PetIdentity* identity = pet_identity_get();

    uint8_t pr, pg, pb, sr, sg, sb;
    pet_identity_get_colors(&pr, &pg, &pb, &sr, &sg, &sb);

    StaticJsonDocument<384> doc;
    doc["hwid"] = identity->hwid;
    doc["name"] = identity->name;
    doc["pattern"] = identity->pattern_seed;

    JsonObject colors = doc.createNestedObject("colors");
    JsonObject primary = colors.createNestedObject("primary");
    primary["r"] = pr;
    primary["g"] = pg;
    primary["b"] = pb;
    primary["hue"] = identity->primary_hue;

    JsonObject secondary = colors.createNestedObject("secondary");
    secondary["r"] = sr;
    secondary["g"] = sg;
    secondary["b"] = sb;
    secondary["hue"] = identity->secondary_hue;

    String output;
    serializeJson(doc, output);
    g_http_server.send(200, "application/json", output);
}

static void handle_api_pet_name_post() {
    if (!g_http_server.hasArg("plain")) {
        g_http_server.send(400, "application/json", "{\"error\":\"No body\"}");
        return;
    }

    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, g_http_server.arg("plain"));

    if (error) {
        g_http_server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* name = doc["name"] | "";

    if (strlen(name) == 0 || strlen(name) > PET_NAME_MAX_LEN) {
        g_http_server.send(400, "application/json", "{\"error\":\"Invalid name length\"}");
        return;
    }

    if (pet_identity_set_name(name)) {
        // Notify WebSocket clients
        StaticJsonDocument<128> notify;
        notify["type"] = "pet_renamed";
        notify["name"] = name;
        String output;
        serializeJson(notify, output);
        g_ws_server.broadcastTXT(output);

        g_http_server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        g_http_server.send(500, "application/json", "{\"error\":\"Save failed\"}");
    }
}

static void handle_not_found() {
    g_http_server.send(404, "text/plain", "Not Found");
}

// WebSocket handler
static void ws_event(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("WS[%u] Disconnected\n", num);
            break;

        case WStype_CONNECTED:
            Serial.printf("WS[%u] Connected\n", num);
            // Send current state immediately
            {
                StaticJsonDocument<256> doc;
                doc["type"] = "connected";
                doc["firmware"] = FIRMWARE_VERSION;
                String output;
                serializeJson(doc, output);
                g_ws_server.sendTXT(num, output);
            }
            break;

        case WStype_TEXT:
            // Handle incoming commands if needed
            Serial.printf("WS[%u] Text: %s\n", num, payload);
            break;

        default:
            break;
    }
}
