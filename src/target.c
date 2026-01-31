#include "target.h"

static const target_info_t targets[] = {
    /* PIC18F27K42 family — ID upper byte 0x6C */
    { 0x6C20, 0xFF00, "PIC18F27K42", 131072 },
    { 0x6C40, 0xFF00, "PIC18F27K42", 131072 },
    { 0x6C00, 0xFF00, "PIC18F26K42", 65536 },
    { 0x6CE0, 0xFF00, "PIC18F25K42", 32768 },
    { 0x6CC0, 0xFF00, "PIC18F24K42", 16384 },
    { 0x6CA0, 0xFF00, "PIC18F47K42", 131072 },
    { 0x6C80, 0xFF00, "PIC18F46K42", 65536 },
    { 0x6C60, 0xFF00, "PIC18F45K42", 32768 },

    /* Q41 family */
    { 0x74E0, 0xFFE0, "PIC18F15Q41", 32768 },
    { 0x7500, 0xFFE0, "PIC18F05Q41", 16384 },
    { 0x7520, 0xFFE0, "PIC18F14Q41", 16384 },
    { 0x7540, 0xFFE0, "PIC18F04Q41", 8192 },
    { 0x7560, 0xFFE0, "PIC18F16Q41", 65536 },
    { 0x7580, 0xFFE0, "PIC18F06Q41", 32768 },

    /* Q40 family */
    { 0x75A0, 0xFFE0, "PIC16F16Q40", 16384 },
    { 0x75C0, 0xFFE0, "PIC18F06Q40", 32768 },
    { 0x75E0, 0xFFE0, "PIC18F15Q40", 32768 },
    { 0x7600, 0xFFE0, "PIC18F05Q40", 16384 },
    { 0x7620, 0xFFE0, "PIC18F14Q40", 16384 },
    { 0x7640, 0xFFE0, "PIC18F04Q40", 8192 },
};

#define TARGET_COUNT (sizeof(targets) / sizeof(targets[0]))

const char *target_identify(uint16_t device_id) {
    for (unsigned i = 0; i < TARGET_COUNT; i++) {
        if ((device_id & targets[i].mask) == (targets[i].id & targets[i].mask))
            return targets[i].name;
    }
    return "Unknown";
}

uint32_t target_flash_size(uint16_t device_id) {
    for (unsigned i = 0; i < TARGET_COUNT; i++) {
        if ((device_id & targets[i].mask) == (targets[i].id & targets[i].mask))
            return targets[i].flash_size;
    }
    return 0;
}
