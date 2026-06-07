#include <stdio.h>
#include <string.h>
#include "cc1101_stm32.h"
#include "config.h"
#include "protocol.h"
#include "stm32_raw_tx_demo.h"

enum {
    CC1101_RX_ADDRESS = 0x00u,
    TX_INTERVAL_MS = 500u
};

static cc1101_t demo_radio;
static uint16_t demo_seq;
static uint32_t last_tx_ms;

static size_t build_demo_packet(uint8_t *out, size_t out_len)
{
    uint8_t tdma_payload[80];
    char text[TDMA_MAX_PAYLOAD];
    tdma_packet_t packet;
    memset(&packet, 0, sizeof(packet));

    int text_len = snprintf(text, sizeof(text), "stm32-rssi:%u", demo_seq);
    if (text_len <= 0 || text_len > TDMA_MAX_PAYLOAD) {
        return 0;
    }

    packet.network_id = TDMA_NETWORK_ID;
    packet.type = TDMA_PKT_DATA;
    packet.src = TDMA_AIRCRAFT_ADDR;
    packet.dst = TDMA_ADDR_BROADCAST;
    packet.frame_no = demo_seq;
    packet.slot_no = TDMA_SLOT_AIRCRAFT_TX;
    packet.payload_len = (uint8_t)text_len;
    memcpy(packet.payload, text, (size_t)text_len);

    size_t payload_len = tdma_encode_payload(&packet, tdma_payload, sizeof(tdma_payload));
    if (payload_len == 0) {
        return 0;
    }

    /* CC1101 ADDR is 0x00 in the shared RF preset, so use address byte 0x00. */
    return cc1101_wrap_variable_packet(CC1101_RX_ADDRESS, tdma_payload, payload_len,
                                       out, out_len);
}

int stm32_raw_tx_demo_init(void)
{
    demo_seq = 0;
    last_tx_ms = 0;
    return cc1101_apply_rf_preset(&demo_radio);
}

int stm32_raw_tx_demo_tick(uint32_t now_ms)
{
    if ((uint32_t)(now_ms - last_tx_ms) < TX_INTERVAL_MS) {
        return 0;
    }
    last_tx_ms = now_ms;

    uint8_t radio_frame[96];
    size_t radio_len = build_demo_packet(radio_frame, sizeof(radio_frame));
    if (radio_len == 0) {
        return -1;
    }

    int rc = cc1101_send_packet(&demo_radio, radio_frame, radio_len);
    if (rc < 0) {
        return rc;
    }
    demo_seq++;
    return 1;
}
