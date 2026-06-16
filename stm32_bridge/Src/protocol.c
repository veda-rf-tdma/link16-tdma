#include "protocol.h"

/**
 * @brief Store a 16-bit value in big-endian order.
 *
 * @param p Destination address for two bytes.
 * @param v Value to store.
 */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

/**
 * @brief Read a 16-bit big-endian value from a byte buffer.
 *
 * @param p Source address containing two bytes.
 * @return Decoded 16-bit value.
 */
static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

uint16_t tdma_crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t tdma_encode_payload(const tdma_packet_t *packet, uint8_t *out, size_t out_len)
{
    const size_t header_len = 8;
    const size_t total_len = header_len + packet->payload_len + 2;
    if (!packet || !out || packet->payload_len > TDMA_MAX_PAYLOAD || out_len < total_len) {
        return 0;
    }

    out[0] = packet->network_id;
    out[1] = packet->type;
    out[2] = packet->src;
    out[3] = packet->dst;
    put_u16(&out[4], packet->frame_no);
    out[6] = packet->slot_no;
    out[7] = packet->payload_len;
    for (uint8_t i = 0; i < packet->payload_len; i++) {
        out[8 + i] = packet->payload[i];
    }

    uint16_t crc = tdma_crc16_ccitt(out, header_len + packet->payload_len);
    put_u16(&out[header_len + packet->payload_len], crc);
    return total_len;
}

int tdma_decode_payload(const uint8_t *data, size_t len, tdma_packet_t *packet)
{
    const size_t header_len = 8;
    if (!data || !packet || len < header_len + 2) {
        return -1;
    }

    uint8_t payload_len = data[7];
    if (payload_len > TDMA_MAX_PAYLOAD || len != header_len + payload_len + 2) {
        return -2;
    }

    uint16_t expected = get_u16(&data[header_len + payload_len]);
    uint16_t actual = tdma_crc16_ccitt(data, header_len + payload_len);
    if (expected != actual) {
        return -3;
    }

    packet->network_id = data[0];
    packet->type = data[1];
    packet->src = data[2];
    packet->dst = data[3];
    packet->frame_no = get_u16(&data[4]);
    packet->slot_no = data[6];
    packet->payload_len = payload_len;
    for (uint8_t i = 0; i < payload_len; i++) {
        packet->payload[i] = data[8 + i];
    }
    return 0;
}

size_t cc1101_wrap_variable_packet(uint8_t cc1101_addr, const uint8_t *payload,
                                   size_t payload_len, uint8_t *out, size_t out_len)
{
    if (!payload || !out || payload_len > 254u || out_len < payload_len + 2u) {
        return 0;
    }

    out[0] = (uint8_t)(payload_len + 1u);
    out[1] = cc1101_addr;
    for (size_t i = 0; i < payload_len; i++) {
        out[2 + i] = payload[i];
    }
    return payload_len + 2u;
}

size_t tdma_encode_join_request(const tdma_join_request_payload_t *payload, uint8_t *out, size_t out_len)
{
    if (!payload || !out || out_len < 12) {
        return 0;
    }
    for (int i = 0; i < 12; i++) {
        out[i] = payload->uid[i];
    }
    return 12;
}

int tdma_decode_join_request(const uint8_t *data, size_t len, tdma_join_request_payload_t *payload)
{
    if (!data || !payload || len < 12) {
        return -1;
    }
    for (int i = 0; i < 12; i++) {
        payload->uid[i] = data[i];
    }
    return 0;
}

size_t tdma_encode_join_accept(const tdma_join_accept_payload_t *payload, uint8_t *out, size_t out_len)
{
    if (!payload || !out || out_len < 14) {
        return 0;
    }
    for (int i = 0; i < 12; i++) {
        out[i] = payload->uid[i];
    }
    out[12] = payload->assigned_addr;
    out[13] = payload->assigned_slot;
    return 14;
}

int tdma_decode_join_accept(const uint8_t *data, size_t len, tdma_join_accept_payload_t *payload)
{
    if (!data || !payload || len < 14) {
        return -1;
    }
    for (int i = 0; i < 12; i++) {
        payload->uid[i] = data[i];
    }
    payload->assigned_addr = data[12];
    payload->assigned_slot = data[13];
    return 0;
}

size_t tdma_encode_anchor_report(const tdma_anchor_report_payload_t *payload, uint8_t *out, size_t out_len)
{
    if (!payload || !out || out_len < 8) return 0;
    out[0] = payload->target_node_id;
    out[1] = payload->rssi_raw;
    out[2] = payload->lqi;
    out[3] = payload->has_relayed_data;
    out[4] = (uint8_t)(payload->relayed_data.echoed_x_cm >> 8);
    out[5] = (uint8_t)(payload->relayed_data.echoed_x_cm & 0xff);
    out[6] = (uint8_t)(payload->relayed_data.echoed_y_cm >> 8);
    out[7] = (uint8_t)(payload->relayed_data.echoed_y_cm & 0xff);
    return 8;
}

int tdma_decode_anchor_report(const uint8_t *data, size_t len, tdma_anchor_report_payload_t *payload)
{
    if (!data || !payload || len < 8) return -1;
    payload->target_node_id = data[0];
    payload->rssi_raw = data[1];
    payload->lqi = data[2];
    payload->has_relayed_data = data[3];
    payload->relayed_data.echoed_x_cm = (int16_t)(((int16_t)data[4] << 8) | data[5]);
    payload->relayed_data.echoed_y_cm = (int16_t)(((int16_t)data[6] << 8) | data[7]);
    return 0;
}

size_t tdma_encode_aircraft_data(const tdma_aircraft_data_payload_t *payload, uint8_t *out, size_t out_len)
{
    if (!payload || !out || out_len < 4) return 0;
    out[0] = (uint8_t)(payload->echoed_x_cm >> 8);
    out[1] = (uint8_t)(payload->echoed_x_cm & 0xff);
    out[2] = (uint8_t)(payload->echoed_y_cm >> 8);
    out[3] = (uint8_t)(payload->echoed_y_cm & 0xff);
    return 4;
}

int tdma_decode_aircraft_data(const uint8_t *data, size_t len, tdma_aircraft_data_payload_t *payload)
{
    if (!data || !payload || len < 4) return -1;
    payload->echoed_x_cm = (int16_t)(((int16_t)data[0] << 8) | data[1]);
    payload->echoed_y_cm = (int16_t)(((int16_t)data[2] << 8) | data[3]);
    return 0;
}

size_t tdma_encode_beacon(const tdma_beacon_payload_t *payload, uint8_t *out, size_t out_len)
{
    if (!payload || !out || out_len < 15) return 0;
    out[0] = (uint8_t)(payload->frame_period_us >> 24);
    out[1] = (uint8_t)(payload->frame_period_us >> 16);
    out[2] = (uint8_t)(payload->frame_period_us >> 8);
    out[3] = (uint8_t)payload->frame_period_us;
    out[4] = (uint8_t)(payload->slot_us >> 24);
    out[5] = (uint8_t)(payload->slot_us >> 16);
    out[6] = (uint8_t)(payload->slot_us >> 8);
    out[7] = (uint8_t)payload->slot_us;
    out[8] = (uint8_t)(payload->guard_us >> 8);
    out[9] = (uint8_t)payload->guard_us;
    out[10] = payload->slot_table_version;
    out[11] = (uint8_t)(payload->aircraft_x_cm >> 8);
    out[12] = (uint8_t)(payload->aircraft_x_cm & 0xff);
    out[13] = (uint8_t)(payload->aircraft_y_cm >> 8);
    out[14] = (uint8_t)(payload->aircraft_y_cm & 0xff);
    return 15;
}

int tdma_decode_beacon(const uint8_t *data, size_t len, tdma_beacon_payload_t *payload)
{
    if (!data || !payload || len < 15) return -1;
    payload->frame_period_us = ((uint32_t)data[0] << 24) |
                               ((uint32_t)data[1] << 16) |
                               ((uint32_t)data[2] << 8) |
                               data[3];
    payload->slot_us = ((uint32_t)data[4] << 24) |
                       ((uint32_t)data[5] << 16) |
                       ((uint32_t)data[6] << 8) |
                       data[7];
    payload->guard_us = ((uint32_t)data[8] << 8) | data[9];
    payload->slot_table_version = data[10];
    payload->aircraft_x_cm = (int16_t)(((int16_t)data[11] << 8) | data[12]);
    payload->aircraft_y_cm = (int16_t)(((int16_t)data[13] << 8) | data[14]);
    return 0;
}
