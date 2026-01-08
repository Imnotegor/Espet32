#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "core_state.h"
#include "rgb_renderer.h"
#include "storage.h"

// Firmware version
#define FIRMWARE_VERSION "1.0.0"
#define FEATURES_SCHEMA_VERSION 1

// Initialize web server (WiFi AP mode)
// ssid and password can be null for default "NeuroPet" / "petpetpet"
bool web_server_init(const char* ssid, const char* password);

// Start the web server (call after init)
void web_server_start(void);

// Stop the web server
void web_server_stop(void);

// Update state for broadcasting (call periodically)
void web_server_update_state(const PetState* state, const BrainOutput* brain,
                             const RGBOutput* rgb);

// Send event to connected clients
void web_server_send_event(const char* event_type, const char* data);

// Process web server events (call in loop)
void web_server_loop(void);

// Get connected client count
uint8_t web_server_get_client_count(void);

// Check if model upload is in progress
bool web_server_is_uploading(void);

// Get WiFi AP IP address
const char* web_server_get_ip(void);

// Callback types for model upload
typedef void (*ModelUploadCallback)(const uint8_t* data, uint32_t size,
                                     const ModelMeta* meta, bool success);
void web_server_set_model_callback(ModelUploadCallback callback);

#endif // WEB_SERVER_H
