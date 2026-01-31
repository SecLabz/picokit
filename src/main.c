#include "icsp.h"
#include "protocol.h"
#include "target.h"
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <pico/binary_info.h>
#include <pico/stdio.h>
#include <pico/stdio_usb.h>
#include <pico/time.h>
#include <stdio.h>
#include <string.h>

#define VERSION_STRING "picokit 1.0"

#define FLASH_ROW_WORDS 64
#define FLASH_ROW_BYTES 128

static bool target_check(void) {
  icsp_enter_lvp();
  uint16_t id = icsp_get_device_id();
  if (id == 0xFFFF || id == 0) {
    icsp_exit_lvp();
    return false;
  }
  return true;
}

static void handle_diag(proto_request_t *req) {
  icsp_enter_lvp();
  uint16_t dev_id = icsp_get_device_id();
  uint16_t rev_id = icsp_get_revision_id();
  icsp_exit_lvp();

  if (dev_id == 0xFFFF || dev_id == 0) {
    proto_send_error(STATUS_ERR_TARGET);
    return;
  }

  const char *name = target_identify(dev_id);
  size_t name_len = strlen(name);

  /* Response: dev_id(2 LE) + rev_id(2 LE) + name(N) */
  uint8_t resp[4 + 64];
  resp[0] = dev_id & 0xFF;
  resp[1] = (dev_id >> 8) & 0xFF;
  resp[2] = rev_id & 0xFF;
  resp[3] = (rev_id >> 8) & 0xFF;
  memcpy(resp + 4, name, name_len);

  proto_send_response(STATUS_OK, resp, (uint16_t)(4 + name_len));
}

static void handle_erase(proto_request_t *req) {
  if (!target_check()) {
    proto_send_error(STATUS_ERR_TARGET);
    return;
  }
  icsp_cmd_erase(ICSP_ERASE_REGION_FLASH | ICSP_ERASE_REGION_EEPROM |
                 ICSP_ERASE_REGION_CONFIG | ICSP_ERASE_REGION_USER_ID);
  icsp_exit_lvp();
  proto_send_ok();
}

static void handle_write_page(proto_request_t *req) {
  /* Payload: addr(4 LE) + data(128) = 132 bytes */
  if (req->len < 4 + FLASH_ROW_BYTES) {
    proto_send_error(STATUS_ERR_PAYLOAD);
    return;
  }

  uint32_t addr = proto_get_u32(req->payload);
  uint8_t *data = req->payload + 4;

  /* Convert bytes to words (little-endian) */
  uint16_t words[FLASH_ROW_WORDS];
  for (int i = 0; i < FLASH_ROW_WORDS; i++)
    words[i] = data[i * 2] | (data[i * 2 + 1] << 8);

  icsp_enter_lvp();
  icsp_program_page((int)addr, words, FLASH_ROW_WORDS, true);
  /* Stay in LVP for subsequent pages */

  proto_send_ok();
}

static void handle_write_config(proto_request_t *req) {
  /* Payload: addr(4 LE) + len(2 LE) + data(N) */
  if (req->len < 6) {
    proto_send_error(STATUS_ERR_PAYLOAD);
    return;
  }

  uint32_t addr = proto_get_u32(req->payload);
  uint16_t data_len = proto_get_u16(req->payload + 4);

  if (req->len < 6 + data_len) {
    proto_send_error(STATUS_ERR_PAYLOAD);
    return;
  }

  icsp_enter_lvp();
  icsp_program_config((int)addr, req->payload + 6, data_len);

  proto_send_ok();
}

static void handle_write_eeprom(proto_request_t *req) {
  /* Payload: addr(4 LE) + len(2 LE) + data(N) */
  if (req->len < 6) {
    proto_send_error(STATUS_ERR_PAYLOAD);
    return;
  }

  uint32_t addr = proto_get_u32(req->payload);
  uint16_t data_len = proto_get_u16(req->payload + 4);

  if (req->len < 6 + data_len) {
    proto_send_error(STATUS_ERR_PAYLOAD);
    return;
  }

  icsp_enter_lvp();
  icsp_program_page_8bit((int)addr, req->payload + 6, data_len, false);

  proto_send_ok();
}

static void handle_read(proto_request_t *req) {
  /* Payload: addr(4 LE) + len(2 LE) — len is in bytes */
  if (req->len < 6) {
    proto_send_error(STATUS_ERR_PAYLOAD);
    return;
  }

  uint32_t addr = proto_get_u32(req->payload);
  uint16_t read_len = proto_get_u16(req->payload + 4);

  if (read_len > PROTO_MAX_PAYLOAD) {
    proto_send_error(STATUS_ERR_PAYLOAD);
    return;
  }

  icsp_enter_lvp();
  uint8_t buf[PROTO_MAX_PAYLOAD];

  if (addr >= 0x310000) {
    /* EEPROM: each word's low byte is one data byte */
    icsp_read_data_8bit((int)addr, buf, read_len);
  } else {
    /* Flash/Config: read words, unpack both bytes (LE) */
    icsp_cmd_loadpc((int)addr);
    uint16_t num_words = (read_len + 1) / 2;
    for (uint16_t i = 0; i < num_words; i++) {
      uint16_t w = icsp_cmd_read_data(true);
      buf[i * 2] = w & 0xFF;
      if (i * 2 + 1 < read_len)
        buf[i * 2 + 1] = (w >> 8) & 0xFF;
    }
  }

  proto_send_response(STATUS_OK, buf, read_len);
}

static void handle_reset_target(proto_request_t *req) {
  icsp_exit_lvp();
  sleep_ms(10);
  proto_send_ok();
}

static void handle_test_eeprom(proto_request_t *req) {
  icsp_enter_lvp();

  uint16_t id = icsp_get_device_id();
  if (id == 0xFFFF || id == 0) {
    icsp_exit_lvp();
    proto_send_error(STATUS_ERR_TARGET);
    return;
  }

  int test_addr = 0x310000;
  uint8_t test_data[4] = {0xBB, 0xCC, 0xDD, 0xEE};
  uint8_t read_data[4] = {0};

  /* Erase EEPROM */
  icsp_cmd_erase(ICSP_ERASE_REGION_EEPROM);
  icsp_exit_lvp();
  sleep_ms(10);
  icsp_enter_lvp();

  /* Write test pattern */
  icsp_cmd_loadpc(test_addr);
  for (int i = 0; i < 4; i++) {
    icsp_cmd_write_data(test_data[i], false);
    sleep_ms(11);
    icsp_cmd_increment_pc();
  }

  icsp_exit_lvp();
  sleep_ms(10);
  icsp_enter_lvp();

  /* Read back */
  icsp_cmd_loadpc(test_addr);
  for (int i = 0; i < 4; i++)
    read_data[i] = icsp_cmd_read_data(true) & 0xFF;

  icsp_exit_lvp();

  /* Compare */
  uint8_t result = 1; /* pass */
  for (int i = 0; i < 4; i++) {
    if (test_data[i] != read_data[i]) {
      result = 0;
      break;
    }
  }

  proto_send_response(STATUS_OK, &result, 1);
}

static void handle_version(proto_request_t *req) {
  const char *ver = VERSION_STRING;
  proto_send_response(STATUS_OK, (const uint8_t *)ver, (uint16_t)strlen(ver));
}

int main(void) {
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
  stdio_init_all();
  stdio_set_translate_crlf(&stdio_usb, false);

  icsp_init(spi0, 29, 7, 6, 4);

  proto_request_t req;

  while (true) {
    if (!proto_read_request(&req))
      continue;

    gpio_put(PICO_DEFAULT_LED_PIN, true);

    switch (req.cmd) {
    case CMD_DIAG:
      handle_diag(&req);
      break;
    case CMD_ERASE:
      handle_erase(&req);
      break;
    case CMD_WRITE_PAGE:
      handle_write_page(&req);
      break;
    case CMD_WRITE_CONFIG:
      handle_write_config(&req);
      break;
    case CMD_WRITE_EEPROM:
      handle_write_eeprom(&req);
      break;
    case CMD_READ:
      handle_read(&req);
      break;
    case CMD_RESET_TARGET:
      handle_reset_target(&req);
      break;
    case CMD_TEST_EEPROM:
      handle_test_eeprom(&req);
      break;
    case CMD_VERSION:
      handle_version(&req);
      break;
    default:
      proto_send_error(STATUS_ERR_CMD);
      break;
    }

    gpio_put(PICO_DEFAULT_LED_PIN, false);
  }

  return 0;
}
