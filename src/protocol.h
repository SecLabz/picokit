#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Command IDs */
#define CMD_DIAG          0x01
#define CMD_ERASE         0x02
#define CMD_WRITE_PAGE    0x03
#define CMD_WRITE_CONFIG  0x04
#define CMD_WRITE_EEPROM  0x05
#define CMD_READ          0x06
#define CMD_RESET_TARGET  0x07
#define CMD_TEST_EEPROM   0x08
#define CMD_VERSION       0x09

/* Status codes */
#define STATUS_OK          0x00
#define STATUS_ERR_CMD     0x01
#define STATUS_ERR_CRC     0x02
#define STATUS_ERR_TARGET  0x03
#define STATUS_ERR_VERIFY  0x04
#define STATUS_ERR_PAYLOAD 0x05

/* Max payload size: 4 (addr) + 128 (page) = 132, round up */
#define PROTO_MAX_PAYLOAD  256

/* Frame structure:
 *   Request:  [CMD:1] [LEN:2 LE] [PAYLOAD:N] [CRC8:1]
 *   Response: [STATUS:1] [LEN:2 LE] [PAYLOAD:N] [CRC8:1]
 */

typedef struct {
    uint8_t cmd;
    uint16_t len;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} proto_request_t;

/* CRC-8 (polynomial 0x07, init 0x00) */
uint8_t proto_crc8(const uint8_t *data, size_t len);

/* Read a complete request frame from stdio. Returns true on success. */
bool proto_read_request(proto_request_t *req);

/* Send a response frame: status byte + optional payload */
void proto_send_response(uint8_t status, const uint8_t *payload, uint16_t len);

/* Convenience: send OK with no payload */
void proto_send_ok(void);

/* Convenience: send error with no payload */
void proto_send_error(uint8_t status);

/* Extract a little-endian uint32 from buffer */
static inline uint32_t proto_get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Extract a little-endian uint16 from buffer */
static inline uint16_t proto_get_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

#endif
