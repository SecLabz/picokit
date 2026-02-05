#ifndef ICSP_H
#define ICSP_H

#include <hardware/spi.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ICSP command opcodes */
enum {
    ICSP_COMMAND_LOAD_PC          = 0x80,
    ICSP_COMMAND_BULK_ERASE       = 0x18,
    ICSP_COMMAND_PAGE_ERASE       = 0xF0,
    ICSP_COMMAND_READ_DATA        = 0xFC,
    ICSP_COMMAND_READ_DATA_INCPC  = 0xFE,
    ICSP_COMMAND_INCREMENT_ADDRESS = 0xF8,
    ICSP_COMMAND_LOAD_DATA        = 0x00,
    ICSP_COMMAND_LOAD_DATA_INCPC  = 0x02,
    ICSP_COMMAND_BEGIN_PROG_INT   = 0xE0,
    ICSP_COMMAND_BEGIN_PROG_EXT   = 0xC0,
    ICSP_COMMAND_END_PROG_EXT    = 0x82,
};

/* Erase region bits for PIC18F27K42 (per programming spec) */
enum {
    ICSP_ERASE_REGION_EEPROM  = (1 << 0),  // Bit 0: Data EEPROM
    ICSP_ERASE_REGION_FLASH   = (1 << 1),  // Bit 1: Flash memory
    ICSP_ERASE_REGION_USER_ID = (1 << 2),  // Bit 2: User ID memory
    ICSP_ERASE_REGION_CONFIG  = (1 << 3),  // Bit 3: Configuration memory
};

void icsp_init(spi_inst_t *spi, int pin_mclr, int pin_dat, int pin_clk,
               int pin_dat_in);
void icsp_enter_lvp(void);
void icsp_exit_lvp(void);

void icsp_send_command(uint8_t cmd, int payload);
void icsp_cmd_loadpc(int pc);
void icsp_cmd_erase(int regions);
void icsp_cmd_erase_page(void);
uint16_t icsp_cmd_read_data(bool increment_pc);
void icsp_cmd_increment_pc(void);
void icsp_cmd_write_data(uint16_t value, bool increment_pc);

uint16_t icsp_get_device_id(void);
uint16_t icsp_get_revision_id(void);

void icsp_read_data_8bit(int addr, uint8_t *data, size_t size);

/* Program a 128-byte flash page (64 words). Erases first if erase=true. */
size_t icsp_program_page(int addr, uint16_t *data, size_t size, bool erase);

/* Program bytes one at a time (for EEPROM). 11ms delay per byte. */
size_t icsp_program_page_8bit(int addr, uint8_t *data, size_t size, bool erase);

/* Program config words one word at a time with proper timing. */
void icsp_program_config(int addr, const uint8_t *data, size_t byte_count);

#endif
