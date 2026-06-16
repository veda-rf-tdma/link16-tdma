#include "usb_cdc_bridge.h"
#include "cc1101_stm32.h"
#include "protocol.h"
#include "stm32_bridge_link.h"

enum {
    BRIDGE_MAGIC = 0xa5u,
    BRIDGE_MAX_FRAME = 128u
};

static cc1101_t bridge_radio;

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

static int bridge_validate_frame(const uint8_t *in, size_t in_len)
{
    if (!in || in_len < 5 || in[0] != BRIDGE_MAGIC) {
        return -1;
    }
    size_t payload_len = in[2];
    if (in_len != payload_len + 5u) {
        return -2;
    }
    uint16_t expected = (uint16_t)(((uint16_t)in[3 + payload_len] << 8) |
                                   in[4 + payload_len]);
    uint16_t actual = tdma_crc16_ccitt(in, payload_len + 3u);
    return expected == actual ? 0 : -3;
}

static int bridge_error(uint8_t code, uint8_t *out, size_t out_len)
{
    return (int)bridge_build_frame(BRIDGE_EVT_ERROR, &code, 1, out, out_len);
}

int usb_cdc_bridge_init(void)
{
    return cc1101_apply_rf_preset(&bridge_radio);
}

int usb_cdc_bridge_parse(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    int rc = bridge_validate_frame(in, in_len);
    if (rc != 0) {
        return bridge_error((uint8_t)(0x10u - rc), out, out_len);
    }

    uint8_t type = in[1];
    const uint8_t *payload = &in[3];
    size_t payload_len = in[2];
    switch (type) {
    case BRIDGE_CMD_START_RX:
        rc = cc1101_start_rx(&bridge_radio);
        return rc < 0 ? bridge_error(0x21, out, out_len) :
                        (int)bridge_build_frame(BRIDGE_EVT_TX_DONE, 0, 0, out, out_len);
    case BRIDGE_CMD_TX_PACKET:
        rc = cc1101_send_packet(&bridge_radio, payload, payload_len);
        cc1101_start_rx(&bridge_radio);
        return rc < 0 ? bridge_error(0x22, out, out_len) :
                        (int)bridge_build_frame(BRIDGE_EVT_TX_DONE, 0, 0, out, out_len);
    case BRIDGE_CMD_GET_STATUS: {
        uint8_t status = bridge_radio.initialized;
        return (int)bridge_build_frame(BRIDGE_EVT_TX_DONE, &status, 1, out, out_len);
    }
    default:
        return bridge_error(0x20, out, out_len);
    }
}

int usb_cdc_bridge_poll_radio(uint8_t *out, size_t out_len)
{
    uint8_t radio_frame[96];
    uint8_t rssi = 0;
    uint8_t lqi = 0;
    int n = cc1101_poll_packet(&bridge_radio, radio_frame, sizeof(radio_frame), &rssi, &lqi);
    if (n <= 0) {
        return 0;
    }
    uint8_t payload[100];
    if ((size_t)n + 2u > sizeof(payload)) {
        return bridge_error(0x30, out, out_len);
    }
    for (int i = 0; i < n; i++) {
        payload[i] = radio_frame[i];
    }
    payload[n] = rssi;
    payload[n + 1] = lqi;
    return (int)bridge_build_frame(BRIDGE_EVT_RX_PACKET, payload, (size_t)n + 2u,
                                   out, out_len);
}
