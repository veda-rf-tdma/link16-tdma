#include "stm32_bridge_link.h"
#include "config.h"
#include "protocol.h"

#define BRIDGE_MAGIC 0xa5u
#define BRIDGE_MAX_FRAME 128u

/**
 * @brief Build one USB CDC frame for the STM32 bridge.
 *
 * The frame layout is [magic][type][payload_len][payload...][crc16].
 *
 * @param type Bridge command or event type.
 * @param payload Optional payload bytes.
 * @param payload_len Number of payload bytes.
 * @param out Destination buffer for the encoded bridge frame.
 * @param out_len Size of @p out.
 * @return Encoded frame length, or 0 if the output buffer is too small.
 */
static size_t bridge_build_frame(uint8_t type, const uint8_t *payload, size_t payload_len,
                                 uint8_t *out, size_t out_len)
{
    if (payload_len > 255u || out_len < payload_len + 5u) {
        return 0;
    }

    out[0] = BRIDGE_MAGIC;
    out[1] = type;
    out[2] = (uint8_t)payload_len;
    for (size_t i = 0; i < payload_len; i++) {
        out[3 + i] = payload[i];
    }
    uint16_t crc = tdma_crc16_ccitt(out, payload_len + 3u);
    out[3 + payload_len] = (uint8_t)(crc >> 8);
    out[4 + payload_len] = (uint8_t)(crc & 0xff);
    return payload_len + 5u;
}

int bridge_open(stm32_bridge_link_t *link, const char *port_name)
{
    return serial_win_open(&link->serial, port_name, WIN_BRIDGE_BAUD);
}

int bridge_start_rx(stm32_bridge_link_t *link)
{
    uint8_t frame[8];
    size_t len = bridge_build_frame(BRIDGE_CMD_START_RX, 0, 0, frame, sizeof(frame));
    return serial_win_write(&link->serial, frame, len) == (int)len ? 0 : -1;
}

int bridge_send_packet(stm32_bridge_link_t *link, const uint8_t *data, size_t len)
{
    uint8_t frame[BRIDGE_MAX_FRAME];
    size_t frame_len = bridge_build_frame(BRIDGE_CMD_TX_PACKET, data, len, frame, sizeof(frame));
    if (frame_len == 0) {
        return -1;
    }
    return serial_win_write(&link->serial, frame, frame_len) == (int)frame_len ? (int)len : -2;
}

int bridge_poll_packet(stm32_bridge_link_t *link, uint8_t *data, size_t max_len)
{
    return bridge_poll_packet_meta(link, data, max_len, 0, 0);
}

int bridge_poll_packet_meta(stm32_bridge_link_t *link, uint8_t *data, size_t max_len,
                            uint8_t *rssi, uint8_t *lqi)
{
    uint8_t hdr[3];
    int n = serial_win_read(&link->serial, hdr, sizeof(hdr));
    if (n <= 0) {
        return 0;
    }
    if (n != 3 || hdr[0] != BRIDGE_MAGIC || hdr[1] != BRIDGE_EVT_RX_PACKET) {
        return -1;
    }
    if (hdr[2] < 2) {
        return -2;
    }
    uint8_t radio_len = (uint8_t)(hdr[2] - 2u);
    if (radio_len > max_len) {
        return -2;
    }

    uint8_t payload[BRIDGE_MAX_FRAME];
    int payload_n = serial_win_read(&link->serial, payload, hdr[2]);
    uint8_t crc_bytes[2];
    int crc_n = serial_win_read(&link->serial, crc_bytes, sizeof(crc_bytes));
    if (payload_n != hdr[2] || crc_n != 2) {
        return -3;
    }

    uint8_t check[BRIDGE_MAX_FRAME];
    if ((size_t)hdr[2] + 3u > sizeof(check)) {
        return -4;
    }
    check[0] = hdr[0];
    check[1] = hdr[1];
    check[2] = hdr[2];
    for (uint8_t i = 0; i < hdr[2]; i++) {
        check[3 + i] = payload[i];
    }

    uint16_t expected = (uint16_t)(((uint16_t)crc_bytes[0] << 8) | crc_bytes[1]);
    uint16_t actual = tdma_crc16_ccitt(check, (size_t)hdr[2] + 3u);
    if (expected != actual) {
        return -5;
    }
    for (uint8_t i = 0; i < radio_len; i++) {
        data[i] = payload[i];
    }
    if (rssi) {
        *rssi = payload[radio_len];
    }
    if (lqi) {
        *lqi = payload[radio_len + 1u];
    }
    return radio_len;
}

void bridge_close(stm32_bridge_link_t *link)
{
    serial_win_close(&link->serial);
}
