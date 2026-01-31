#ifndef TARGET_H
#define TARGET_H

#include <stdint.h>

/* Memory map for PIC18F27K42 family */
#define FLASH_START       0x000000
#define FLASH_END         0x01FFFF   /* 128KB */
#define FLASH_PAGE_SIZE   128        /* bytes */

#define EEPROM_START      0x310000
#define EEPROM_END        0x3103FF   /* 1KB */

#define CONFIG_START      0x300000
#define CONFIG_END        0x30000F

#define DEVID_ADDR        0x3FFFFE
#define REVID_ADDR        0x3FFFFC

typedef struct {
    uint16_t id;       /* device ID (masked) */
    uint16_t mask;     /* mask to apply before comparing */
    const char *name;
    uint32_t flash_size;
} target_info_t;

/* Look up chip name by device ID. Returns "Unknown" if not found. */
const char *target_identify(uint16_t device_id);

/* Get flash size for a given device ID. Returns 0 if unknown. */
uint32_t target_flash_size(uint16_t device_id);

#endif
