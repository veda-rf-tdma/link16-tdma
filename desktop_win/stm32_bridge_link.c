#include <stdio.h>
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

    // Wait for STM32 to boot and send its startup logs (e.g. MASTER_BOOT_OK)
    Sleep(1200);

    // Read and print any boot/startup logs
    uint8_t boot_buf[256];
    int boot_n = serial_win_read(&link->serial, boot_buf, sizeof(boot_buf) - 1);
    if (boot_n > 0) {
        boot_buf[boot_n] = '\0';
        fprintf(stderr, "[INFO] Received STM32 startup log:\n");
        fprintf(stderr, "%s", boot_buf);
        if (boot_buf[boot_n - 1] != '\n') {
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "--------------------------------------------------\n");
    }

    for (int retry = 0; retry < 3; retry++) {
        if (serial_win_write(&link->serial, frame, len) != (int)len) {
            Sleep(100);
            continue;
        }

        // Wait for response (timeout 500ms per attempt)
        // Find the magic byte 0xA5 first to align with the frame
        uint8_t magic = 0;
        int found_magic = 0;
#ifdef _WIN32
        DWORD start_time = GetTickCount();
        while ((GetTickCount() - start_time) < 500) {
            int n = serial_win_read(&link->serial, &magic, 1);
            if (n > 0 && magic == BRIDGE_MAGIC) {
                found_magic = 1;
                break;
            }
            Sleep(10);
        }
#else
        found_magic = 1;
        magic = BRIDGE_MAGIC;
#endif

        if (!found_magic) {
            Sleep(100);
            continue;
        }

        // Read the next 2 bytes (type and payload length)
        uint8_t header[2];
        int header_bytes = 0;
#ifdef _WIN32
        DWORD header_start = GetTickCount();
        while (header_bytes < 2 && (GetTickCount() - header_start) < 300) {
            int n = serial_win_read(&link->serial, header + header_bytes, 2 - header_bytes);
            if (n > 0) {
                header_bytes += n;
            } else {
                Sleep(5);
            }
        }
#else
        header_bytes = 2;
        header[0] = BRIDGE_EVT_TX_DONE;
        header[1] = 0;
#endif

        if (header_bytes < 2) {
            Sleep(100);
            continue;
        }

        uint8_t type = header[0];
        uint8_t len_payload = header[1];

        // Now read the payload and CRC.
        // We need len_payload bytes of payload + 2 bytes of CRC.
        int expected_remaining = len_payload + 2;
        uint8_t remaining[16];
        int remaining_bytes = 0;
#ifdef _WIN32
        DWORD remaining_start = GetTickCount();
        while (remaining_bytes < expected_remaining && (GetTickCount() - remaining_start) < 300) {
            int n = serial_win_read(&link->serial, remaining + remaining_bytes, expected_remaining - remaining_bytes);
            if (n > 0) {
                remaining_bytes += n;
            } else {
                Sleep(5);
            }
        }
#else
        remaining_bytes = expected_remaining;
        remaining[0] = 0;
        remaining[1] = 0;
#endif

        if (remaining_bytes < expected_remaining) {
            Sleep(100);
            continue;
        }

        // Reconstruct frame for CRC check
        uint8_t check[32];
        check[0] = BRIDGE_MAGIC;
        check[1] = type;
        check[2] = len_payload;
        for (int i = 0; i < len_payload; i++) {
            check[3 + i] = remaining[i];
        }

        uint16_t expected_crc = (uint16_t)(((uint16_t)remaining[len_payload] << 8) | remaining[len_payload + 1]);
        uint16_t actual_crc = tdma_crc16_ccitt(check, len_payload + 3);

        if (expected_crc != actual_crc) {
            Sleep(100);
            continue;
        }

        // Process successful parse
        if (type == BRIDGE_EVT_TX_DONE) {
            return 0; // Success!
        } else if (type == BRIDGE_EVT_ERROR) {
            return -3; // Master replied with error (e.g. CC1101 failed)
        }
    }

#ifdef _WIN32
    // If we timed out, let's see what is in the buffer (if anything)
    uint8_t debug_buf[128];
    int debug_n = serial_win_read(&link->serial, debug_buf, sizeof(debug_buf) - 1);
    if (debug_n > 0) {
        debug_buf[debug_n] = '\0';
        fprintf(stderr, "\n[DEBUG] Port sent %d bytes: '%s'\n", debug_n, debug_buf);
        fprintf(stderr, "[DEBUG] Hex bytes: ");
        for (int j = 0; j < debug_n; j++) {
            fprintf(stderr, "%02X ", debug_buf[j]);
        }
        fprintf(stderr, "\n\n");
    } else {
        fprintf(stderr, "\n[DEBUG] Port sent 0 bytes (no response).\n\n");
    }
#endif

    return -2; // Timeout after retries
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
    static uint8_t rx_buf[1024];
    static size_t rx_idx = 0;

    // 1. Read all available bytes from serial port
    uint8_t temp[256];
    int n = serial_win_read(&link->serial, temp, sizeof(temp));
    if (n > 0) {
        if (rx_idx + (size_t)n <= sizeof(rx_buf)) {
            memcpy(rx_buf + rx_idx, temp, (size_t)n);
            rx_idx += (size_t)n;
        } else {
            // Buffer overflow safety: discard buffer and start fresh
            rx_idx = 0;
        }
    }

    // 2. Process buffer to find valid frames
    while (rx_idx >= 5) {
        int start = -1;
        for (size_t i = 0; i < rx_idx; i++) {
            if (rx_buf[i] == BRIDGE_MAGIC) {
                start = (int)i;
                break;
            }
        }

        // If no Magic byte is found, print all bytes and discard
        if (start < 0) {
            for (size_t i = 0; i < rx_idx; i++) {
                putchar(rx_buf[i]);
            }
            fflush(stdout);
            rx_idx = 0;
            break;
        }

        // Shift buffer to align start with BRIDGE_MAGIC, printing non-magic bytes
        if (start > 0) {
            for (int i = 0; i < start; i++) {
                putchar(rx_buf[i]);
            }
            fflush(stdout);
            memmove(rx_buf, rx_buf + start, rx_idx - (size_t)start);
            rx_idx -= (size_t)start;
        }

        // Sane check headers
        uint8_t type = rx_buf[1];
        uint8_t payload_len = rx_buf[2];
        size_t expected_len = (size_t)payload_len + 5u;

        if (type != BRIDGE_EVT_RX_PACKET && type != BRIDGE_EVT_TX_DONE && type != BRIDGE_EVT_ERROR) {
            // False start Magic byte, shift by 1 and retry
            memmove(rx_buf, rx_buf + 1, rx_idx - 1);
            rx_idx -= 1;
            continue;
        }

        if (expected_len > 128) {
            // False start Magic byte due to invalid length, shift by 1 and retry
            memmove(rx_buf, rx_buf + 1, rx_idx - 1);
            rx_idx -= 1;
            continue;
        }

        // If the complete frame is not yet received, wait for next poll
        if (rx_idx < expected_len) {
            break;
        }

        // Verify CRC
        uint16_t expected_crc = (uint16_t)(((uint16_t)rx_buf[expected_len - 2] << 8) | rx_buf[expected_len - 1]);
        uint16_t actual_crc = tdma_crc16_ccitt(rx_buf, expected_len - 2);

        if (expected_crc != actual_crc) {
            // CRC mismatch, shift by 1 and retry
            memmove(rx_buf, rx_buf + 1, rx_idx - 1);
            rx_idx -= 1;
            continue;
        }

        // Valid frame parsed!
        if (type == BRIDGE_EVT_RX_PACKET) {
            if (payload_len < 2) {
                memmove(rx_buf, rx_buf + expected_len, rx_idx - expected_len);
                rx_idx -= expected_len;
                return -2;
            }
            uint8_t radio_len = (uint8_t)(payload_len - 2u);
            if (radio_len > max_len) {
                memmove(rx_buf, rx_buf + expected_len, rx_idx - expected_len);
                rx_idx -= expected_len;
                return -2;
            }

            for (size_t i = 0; i < radio_len; i++) {
                data[i] = rx_buf[3 + i];
            }
            if (rssi) {
                *rssi = rx_buf[3 + radio_len];
            }
            if (lqi) {
                *lqi = rx_buf[3 + radio_len + 1u];
            }

            memmove(rx_buf, rx_buf + expected_len, rx_idx - expected_len);
            rx_idx -= expected_len;
            return (int)radio_len;
        } else {
            // Consume non-RX events (like TX_DONE, ERROR) and continue parsing
            if (type == BRIDGE_EVT_ERROR && payload_len >= 1) {
                fprintf(stderr, "\n[ERROR] Master Bridge Error Event: code 0x%02X\n", rx_buf[3]);
            }
            memmove(rx_buf, rx_buf + expected_len, rx_idx - expected_len);
            rx_idx -= expected_len;
            continue;
        }
    }

    return 0;
}

void bridge_close(stm32_bridge_link_t *link)
{
    serial_win_close(&link->serial);
}
