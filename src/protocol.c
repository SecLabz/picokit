#include "protocol.h"
#include <pico/stdio.h>
#include <stdio.h>
#include <string.h>

uint8_t proto_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
    }
    return crc;
}

bool proto_read_request(proto_request_t *req) {
    int c;

    /* Read command byte */
    c = getchar();
    if (c == PICO_ERROR_TIMEOUT || c < 0)
        return false;
    req->cmd = (uint8_t)c;

    /* Read length (2 bytes LE) */
    int lo = getchar();
    int hi = getchar();
    if (lo < 0 || hi < 0)
        return false;
    req->len = (uint16_t)lo | ((uint16_t)hi << 8);

    if (req->len > PROTO_MAX_PAYLOAD) {
        /* Drain excess bytes */
        for (uint16_t i = 0; i < req->len + 1; i++)
            getchar();
        req->len = 0;
        return false;
    }

    /* Read payload */
    for (uint16_t i = 0; i < req->len; i++) {
        c = getchar();
        if (c < 0)
            return false;
        req->payload[i] = (uint8_t)c;
    }

    /* Read and verify CRC */
    c = getchar();
    if (c < 0)
        return false;
    uint8_t received_crc = (uint8_t)c;

    /* Compute CRC over cmd + len_lo + len_hi + payload */
    uint8_t frame[3 + PROTO_MAX_PAYLOAD];
    frame[0] = req->cmd;
    frame[1] = (uint8_t)(req->len & 0xFF);
    frame[2] = (uint8_t)(req->len >> 8);
    memcpy(frame + 3, req->payload, req->len);
    uint8_t expected_crc = proto_crc8(frame, 3 + req->len);

    if (received_crc != expected_crc) {
        proto_send_error(STATUS_ERR_CRC);
        return false;
    }

    return true;
}

void proto_send_response(uint8_t status, const uint8_t *payload, uint16_t len) {
    /* Build complete frame in one buffer to avoid USB CDC fragmentation */
    uint8_t frame[4 + PROTO_MAX_PAYLOAD]; /* header(3) + payload + crc(1) */
    frame[0] = status;
    frame[1] = (uint8_t)(len & 0xFF);
    frame[2] = (uint8_t)(len >> 8);
    if (len > 0 && payload)
        memcpy(frame + 3, payload, len);
    frame[3 + len] = proto_crc8(frame, 3 + len);

    uint16_t total = 4 + len;
    for (uint16_t i = 0; i < total; i++)
        putchar(frame[i]);
    stdio_flush();
}

void proto_send_ok(void) {
    proto_send_response(STATUS_OK, NULL, 0);
}

void proto_send_error(uint8_t status) {
    proto_send_response(status, NULL, 0);
}
