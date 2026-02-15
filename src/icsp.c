#include "icsp.h"
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <pico/time.h>

// Credit: https://github.com/MCJack123/pico-icsp-programmer
static int dataOutPin = PICO_DEFAULT_SPI_TX_PIN;
static int dataInPin = PICO_DEFAULT_SPI_RX_PIN;
static int clockPin = PICO_DEFAULT_SPI_SCK_PIN;
static int mclrPin;
static spi_inst_t *spi_inst;

static bool lvp_active = false;

void icsp_init(spi_inst_t *spi, int pin_mclr, int pin_dat, int pin_clk,
               int pin_dat_in) {
  gpio_init(pin_mclr);
  gpio_init(pin_dat);
  gpio_init(pin_clk);
  gpio_set_function(pin_mclr, GPIO_FUNC_SIO);
  gpio_set_function(pin_dat, GPIO_FUNC_SPI);
  gpio_set_function(pin_clk, GPIO_FUNC_SPI);
  gpio_set_dir(pin_mclr, GPIO_OUT);
  gpio_set_dir(pin_dat, GPIO_OUT);
  gpio_set_dir(pin_clk, GPIO_OUT);
  if (pin_dat_in != -1) {
    gpio_init(pin_dat_in);
    gpio_set_function(pin_dat_in, GPIO_FUNC_NULL);
    gpio_set_dir(pin_dat_in, GPIO_IN);
  }
  gpio_put(pin_mclr, true);
  spi_init(spi, 5000000);
  spi_set_slave(spi, false);
  spi_set_format(spi, 8, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);
  dataOutPin = pin_dat;
  dataInPin = pin_dat_in;
  clockPin = pin_clk;
  mclrPin = pin_mclr;
  spi_inst = spi;
}

void icsp_enter_lvp(void) {
  if (lvp_active)
    return;
  gpio_put(mclrPin, 0);
  sleep_ms(50);
  spi_write_blocking(spi_inst, (const uint8_t *)"MCHP", 4);
  sleep_ms(5);
  lvp_active = true;
}

void icsp_exit_lvp(void) {
  if (!lvp_active)
    return;
  gpio_put(mclrPin, 1);
  sleep_ms(5);
  lvp_active = false;
}

void icsp_send_command(uint8_t cmd, int payload)
    __attribute__((optimize("O0")));
void icsp_send_command(uint8_t cmd, int payload) {
  if (payload >= 0) {
    uint8_t buf[4] = {cmd, (uint8_t)((payload >> 15) & 0xFF),
                      (uint8_t)((payload >> 7) & 0xFF),
                      (uint8_t)((payload << 1) & 0xFF)};
    spi_write_blocking(spi_inst, buf, 4);
  } else {
    spi_write_blocking(spi_inst, &cmd, 1);
  }
}

void icsp_cmd_loadpc(int pc) { icsp_send_command(ICSP_COMMAND_LOAD_PC, pc); }

void icsp_cmd_erase(int regions) {
  // Bulk Erase (0x18) takes NO payload. The current PC value
  // determines which memory region is erased.
  // Erase config first — on CP-protected devices this triggers full chip erase.
  if (regions & ICSP_ERASE_REGION_CONFIG) {
    icsp_cmd_loadpc(0x300000);
    icsp_send_command(ICSP_COMMAND_BULK_ERASE, -1);
    sleep_ms(26);
  }
  if (regions & ICSP_ERASE_REGION_FLASH) {
    icsp_cmd_loadpc(0x000000);
    icsp_send_command(ICSP_COMMAND_BULK_ERASE, -1);
    sleep_ms(26);
  }
  if (regions & ICSP_ERASE_REGION_EEPROM) {
    icsp_cmd_loadpc(0x310000);
    icsp_send_command(ICSP_COMMAND_BULK_ERASE, -1);
    sleep_ms(26);
  }
  if (regions & ICSP_ERASE_REGION_USER_ID) {
    icsp_cmd_loadpc(0x200000);
    icsp_send_command(ICSP_COMMAND_BULK_ERASE, -1);
    sleep_ms(26);
  }
}

void icsp_cmd_erase_page(void) {
  icsp_send_command(ICSP_COMMAND_PAGE_ERASE, -1);
  sleep_ms(11);
}

uint16_t icsp_cmd_read_data(bool increment_pc) {
  if (dataInPin == -1)
    return 0xFFFF;
  uint8_t cmd =
      increment_pc ? ICSP_COMMAND_READ_DATA_INCPC : ICSP_COMMAND_READ_DATA;
  spi_write_blocking(spi_inst, &cmd, 1);
  gpio_set_function(dataOutPin, GPIO_FUNC_NULL);
  gpio_set_function(dataInPin, GPIO_FUNC_SPI);
  uint8_t data[3];
  spi_read_blocking(spi_inst, 0, data, 3);
  gpio_set_function(dataInPin, GPIO_FUNC_NULL);
  gpio_set_function(dataOutPin, GPIO_FUNC_SPI);
  return (data[0] << 15) | (data[1] << 7) | (data[2] >> 1);
}

void icsp_cmd_increment_pc(void) {
  icsp_send_command(ICSP_COMMAND_INCREMENT_ADDRESS, -1);
}

void icsp_cmd_write_data(uint16_t value, bool increment_pc) {
  icsp_send_command(increment_pc ? ICSP_COMMAND_LOAD_DATA_INCPC
                                 : ICSP_COMMAND_LOAD_DATA,
                    value);
  icsp_send_command(ICSP_COMMAND_BEGIN_PROG_INT, -1);
  sleep_us(75);
}

uint16_t icsp_get_device_id(void) {
  icsp_cmd_loadpc(0x3FFFFE);
  sleep_us(1);
  return icsp_cmd_read_data(false);
}

uint16_t icsp_get_revision_id(void) {
  icsp_cmd_loadpc(0x3FFFFC);
  sleep_us(1);
  return icsp_cmd_read_data(false);
}

void icsp_read_data_8bit(int addr, uint8_t *data, size_t size) {
  icsp_cmd_loadpc(addr);
  for (size_t i = 0; i < size; i++)
    data[i] = icsp_cmd_read_data(true) & 0xFF;
}

size_t icsp_program_page(int addr, uint16_t *data, size_t size, bool erase) {
  icsp_cmd_loadpc(addr);
  if (erase)
    icsp_cmd_erase_page();
  for (size_t i = 0; i < size - 1; i++)
    icsp_send_command(ICSP_COMMAND_LOAD_DATA_INCPC, data[i]);
  icsp_send_command(ICSP_COMMAND_LOAD_DATA, data[size - 1]);
  icsp_send_command(ICSP_COMMAND_BEGIN_PROG_INT, -1);
  sleep_ms(3);
  return size;
}

size_t icsp_program_page_8bit(int addr, uint8_t *data, size_t size,
                              bool erase) {
  icsp_cmd_loadpc(addr);
  if (erase)
    icsp_cmd_erase_page();
  for (size_t i = 0; i < size; i++) {
    icsp_cmd_write_data(data[i], true);
    sleep_ms(11);
  }
  return size;
}

void icsp_program_config(int addr, const uint8_t *data, size_t byte_count) {
  // Config words must always be written explicitly, even if 0xFFFF.
  // Some config bits (e.g. CP) have an erased state that is NOT 0xFF,
  // so skipping 0xFFFF words can leave code protection enabled.
  for (size_t i = 0; i + 1 < byte_count; i += 2) {
    uint16_t word = data[i] | (data[i + 1] << 8);
    icsp_cmd_loadpc(addr + (int)i);
    icsp_send_command(ICSP_COMMAND_LOAD_DATA, word);
    icsp_send_command(ICSP_COMMAND_BEGIN_PROG_INT, -1);
    sleep_ms(11);
  }
  if (byte_count & 1) {
    uint16_t word = data[byte_count - 1] | 0xFF00;
    icsp_cmd_loadpc(addr + (int)(byte_count - 1));
    icsp_send_command(ICSP_COMMAND_LOAD_DATA, word);
    icsp_send_command(ICSP_COMMAND_BEGIN_PROG_INT, -1);
    sleep_ms(11);
  }
}
