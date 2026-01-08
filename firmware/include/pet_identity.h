#pragma once
#include <stdint.h>

// Maximum pet name length
#define PET_NAME_MAX_LEN 24

// Pet identity structure
typedef struct {
    char hwid[13];              // 12 hex chars + null (from MAC address)
    char name[PET_NAME_MAX_LEN + 1];
    uint8_t primary_hue;        // 0-255, primary color hue
    uint8_t secondary_hue;      // 0-255, secondary color hue
    uint8_t pattern_seed;       // For pattern variations
} PetIdentity;

// Initialize pet identity (generates HWID, loads name)
void pet_identity_init(void);

// Get current pet identity
const PetIdentity* pet_identity_get(void);

// Set pet name (saves to NVS)
bool pet_identity_set_name(const char* name);

// Get HWID string
const char* pet_identity_get_hwid(void);

// Get pet name
const char* pet_identity_get_name(void);

// Get color palette based on HWID
// Returns RGB values for primary and secondary colors
void pet_identity_get_colors(uint8_t* primary_r, uint8_t* primary_g, uint8_t* primary_b,
                             uint8_t* secondary_r, uint8_t* secondary_g, uint8_t* secondary_b);

// Get pattern seed for model variations
uint8_t pet_identity_get_pattern(void);
