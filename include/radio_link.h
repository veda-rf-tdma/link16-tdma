#ifndef RADIO_LINK_H
#define RADIO_LINK_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int rssi_dbm;
    uint8_t lqi;
    uint8_t crc_ok;
} radio_rx_meta_t;

typedef struct radio_link radio_link_t;

/**
 * @brief Open a concrete radio backend.
 *
 * This is a future abstraction point for STM32 bridge or
 * another backend. It is declared but not implemented in the MVP.
 */
int radio_link_open(radio_link_t *radio);

/**
 * @brief Configure the radio backend with the current CC1101 RF preset.
 */
int radio_link_configure(radio_link_t *radio);

/**
 * @brief Put the radio backend into RX mode.
 */
int radio_link_start_rx(radio_link_t *radio);

/**
 * @brief Send bytes through the radio backend.
 *
 * @param radio Backend instance.
 * @param data Bytes to send.
 * @param len Number of bytes to send.
 * @return Sent byte count or negative error code.
 */
int radio_link_send(radio_link_t *radio, const uint8_t *data, size_t len);

/**
 * @brief Poll for received bytes and optional RSSI/LQI metadata.
 *
 * @param radio Backend instance.
 * @param data Destination buffer.
 * @param max_len Destination buffer size.
 * @param meta Optional metadata output.
 * @return Received byte count, 0 if none, or negative error code.
 */
int radio_link_poll_rx(radio_link_t *radio, uint8_t *data, size_t max_len,
                       radio_rx_meta_t *meta);

/**
 * @brief Close a concrete radio backend.
 */
void radio_link_close(radio_link_t *radio);

#endif
