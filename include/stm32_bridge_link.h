#ifndef STM32_BRIDGE_LINK_H
#define STM32_BRIDGE_LINK_H

#include <stddef.h>
#include <stdint.h>
#include "serial_win.h"

typedef struct {
    serial_win_t serial;
} stm32_bridge_link_t;

typedef enum {
    BRIDGE_CMD_SET_CONFIG = 1,
    BRIDGE_CMD_TX_PACKET = 2,
    BRIDGE_CMD_START_RX = 3,
    BRIDGE_CMD_GET_STATUS = 4,
    BRIDGE_EVT_RX_PACKET = 0x81,
    BRIDGE_EVT_TX_DONE = 0x82,
    BRIDGE_EVT_ERROR = 0x83
} bridge_msg_type_t;

/**
 * @brief Open the STM32 USB CDC radio bridge.
 *
 * @param link Bridge connection state.
 * @param port_name Windows COM port name such as COM3.
 * @return 0 on success, negative error code on failure.
 */
int bridge_open(stm32_bridge_link_t *link, const char *port_name);

/**
 * @brief Ask the STM32 bridge to put its CC1101 in RX mode.
 *
 * @param link Open bridge connection.
 * @return 0 on success, negative error code on failure.
 */
int bridge_start_rx(stm32_bridge_link_t *link);

/**
 * @brief Send one already-wrapped CC1101 packet through the STM32 bridge.
 *
 * @param link Open bridge connection.
 * @param data CC1101 FIFO bytes, usually [length][address][TDMA payload...].
 * @param len Number of bytes in @p data.
 * @return Original packet length on success, negative error code on failure.
 */
int bridge_send_packet(stm32_bridge_link_t *link, const uint8_t *data, size_t len);

/**
 * @brief Poll the STM32 bridge for a received radio packet event.
 *
 * @param link Open bridge connection.
 * @param data Destination buffer for received bytes.
 * @param max_len Size of @p data.
 * @return Received byte count, 0 if no packet is available, or negative error code.
 */
int bridge_poll_packet(stm32_bridge_link_t *link, uint8_t *data, size_t max_len);

/**
 * @brief Poll the STM32 bridge for a received radio packet event with RF metadata.
 *
 * STM32 RX events carry [radio_frame...][rssi][lqi]. This function strips the
 * trailing metadata and returns only radio_frame length.
 *
 * @param link Open bridge connection.
 * @param data Destination buffer for received radio frame bytes.
 * @param max_len Size of @p data.
 * @param rssi Optional destination for CC1101 raw RSSI byte.
 * @param lqi Optional destination for CC1101 raw LQI byte.
 * @return Received radio frame byte count, 0 if no packet is available, or negative error code.
 */
int bridge_poll_packet_meta(stm32_bridge_link_t *link, uint8_t *data, size_t max_len,
                            uint8_t *rssi, uint8_t *lqi);

/**
 * @brief Close the STM32 bridge connection.
 *
 * @param link Bridge connection state.
 */
void bridge_close(stm32_bridge_link_t *link);

#endif
