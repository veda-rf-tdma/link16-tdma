#ifndef TDMA_PROTOCOL_H
#define TDMA_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "config.h"

typedef enum {
    TDMA_PKT_BEACON = 1,
    TDMA_PKT_SLOT_MAP = 2,
    TDMA_PKT_ACK = 3,
    TDMA_PKT_DATA = 4,
    TDMA_PKT_GATEWAY_STATUS = 5,
    TDMA_PKT_JOIN_REQUEST = 6,
    TDMA_PKT_P2P_REQUEST = 7,
    TDMA_PKT_JOIN_ACCEPT = 8,
    TDMA_PKT_ANCHOR_REPORT = 9
} tdma_packet_type_t;

typedef struct {
    uint8_t uid[12];
} tdma_join_request_payload_t;

typedef struct {
    uint8_t uid[12];
    uint8_t assigned_addr;
    uint8_t assigned_slot;
} tdma_join_accept_payload_t;

typedef struct {
    int16_t echoed_x_cm;
    int16_t echoed_y_cm;
} tdma_aircraft_data_payload_t;

typedef struct {
    uint8_t target_node_id;
    uint8_t rssi_raw;
    uint8_t lqi;
    uint8_t has_relayed_data;
    tdma_aircraft_data_payload_t relayed_data;
} tdma_anchor_report_payload_t;

typedef struct {
    uint8_t network_id;
    uint8_t type;
    uint8_t src;
    uint8_t dst;
    uint16_t frame_no;
    uint8_t slot_no;
    uint8_t payload_len;
    uint8_t payload[TDMA_MAX_PAYLOAD];
} tdma_packet_t;

typedef struct {
    uint16_t frame_no;
    uint32_t frame_period_us;
    uint32_t slot_us;
    uint32_t guard_us;
    uint8_t slot_table_version;
    int16_t aircraft_x_cm;
    int16_t aircraft_y_cm;
} tdma_beacon_payload_t;

/**
 * @brief Encode a TDMA packet structure into the on-air TDMA payload format.
 *
 * The encoded layout is:
 * network_id, type, src, dst, frame_no, slot_no, payload_len, payload, crc16.
 *
 * @param packet Packet fields to encode.
 * @param out Output byte buffer.
 * @param out_len Size of the output buffer.
 * @return Encoded byte count, or 0 if the input is invalid or the buffer is too small.
 */
size_t tdma_encode_payload(const tdma_packet_t *packet, uint8_t *out, size_t out_len);

/**
 * @brief Decode and validate an on-air TDMA payload.
 *
 * This checks the software CRC16 before copying fields into @p packet.
 *
 * @param data Encoded TDMA payload bytes.
 * @param len Number of bytes in @p data.
 * @param packet Destination structure for decoded fields.
 * @return 0 on success, negative error code on malformed input or CRC failure.
 */
int tdma_decode_payload(const uint8_t *data, size_t len, tdma_packet_t *packet);

/**
 * @brief Wrap a TDMA payload for the CC1101 variable-length packet mode.
 *
 * RF Studio currently uses variable packet length and address check, so the
 * CC1101 TX FIFO must receive [length][address][payload...].
 *
 * @param cc1101_addr CC1101 address-check byte.
 * @param payload TDMA payload bytes.
 * @param payload_len Number of TDMA payload bytes.
 * @param out Output buffer for CC1101 FIFO bytes.
 * @param out_len Size of the output buffer.
 * @return Wrapped byte count, or 0 if the input is invalid or too large.
 */
size_t cc1101_wrap_variable_packet(uint8_t cc1101_addr, const uint8_t *payload,
                                   size_t payload_len, uint8_t *out, size_t out_len);

/**
 * @brief Calculate the TDMA software CRC16 using CRC-CCITT.
 *
 * @param data Bytes to checksum.
 * @param len Number of bytes to checksum.
 * @return CRC16 value.
 */
uint16_t tdma_crc16_ccitt(const uint8_t *data, size_t len);

size_t tdma_encode_join_request(const tdma_join_request_payload_t *payload, uint8_t *out, size_t out_len);
int tdma_decode_join_request(const uint8_t *data, size_t len, tdma_join_request_payload_t *payload);
size_t tdma_encode_join_accept(const tdma_join_accept_payload_t *payload, uint8_t *out, size_t out_len);
int tdma_decode_join_accept(const uint8_t *data, size_t len, tdma_join_accept_payload_t *payload);

size_t tdma_encode_anchor_report(const tdma_anchor_report_payload_t *payload, uint8_t *out, size_t out_len);
int tdma_decode_anchor_report(const uint8_t *data, size_t len, tdma_anchor_report_payload_t *payload);

size_t tdma_encode_aircraft_data(const tdma_aircraft_data_payload_t *payload, uint8_t *out, size_t out_len);
int tdma_decode_aircraft_data(const uint8_t *data, size_t len, tdma_aircraft_data_payload_t *payload);

size_t tdma_encode_beacon(const tdma_beacon_payload_t *payload, uint8_t *out, size_t out_len);
int tdma_decode_beacon(const uint8_t *data, size_t len, tdma_beacon_payload_t *payload);

#endif
