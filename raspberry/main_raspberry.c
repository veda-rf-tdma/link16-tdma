#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#endif
#include "cc1101_linux.h"
#include "config.h"
#include "protocol.h"
#include "radio_metrics.h"
#include "tdma.h"

/**
 * @brief Read the local monotonic clock in microseconds.
 *
 * Raspberry TDMA sync must use a monotonic clock rather than wall-clock time,
 * because NTP or manual time changes must not move slot timing.
 *
 * @return CLOCK_MONOTONIC timestamp in microseconds on Linux, 0 on Windows.
 */
static long long monotonic_us(void)
{
#ifndef _WIN32
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((long long)ts.tv_sec * 1000000LL) + ((long long)ts.tv_nsec / 1000LL);
#else
    return 0;
#endif
}

static void sleep_ms(long ms)
{
#ifndef _WIN32
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
#else
    (void)ms;
#endif
}

static int print_part_info(const char *name, cc1101_linux_t *radio)
{
    uint8_t partnum = 0;
    uint8_t version = 0;
    int rc = cc1101_linux_read_part_info(radio, &partnum, &version);
    if (rc < 0) {
        printf("%s part info read failed: %d\n", name, rc);
        return rc;
    }
    printf("%s PARTNUM=0x%02x VERSION=0x%02x\n", name, partnum, version);
    return 0;
}

static size_t build_range_packet(uint16_t seq, uint8_t *out, size_t out_len)
{
    uint8_t tdma_payload[80];
    char message[TDMA_MAX_PAYLOAD];
    tdma_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    int message_len = snprintf(message, sizeof(message), "range-ping:%u", seq);
    if (message_len <= 0 || message_len > TDMA_MAX_PAYLOAD) {
        return 0;
    }
    packet.network_id = TDMA_NETWORK_ID;
    packet.type = TDMA_PKT_DATA;
    packet.src = TDMA_ADDR_RASPBERRY;
    packet.dst = TDMA_ADDR_BROADCAST;
    packet.frame_no = seq;
    packet.slot_no = 2;
    packet.payload_len = (uint8_t)message_len;
    memcpy(packet.payload, message, (size_t)message_len);

    size_t payload_len = tdma_encode_payload(&packet, tdma_payload, sizeof(tdma_payload));
    if (payload_len == 0) {
        return 0;
    }
    return cc1101_wrap_variable_packet(0x00, tdma_payload, payload_len, out, out_len);
}

static int run_packet_test(unsigned long packet_count)
{
    cc1101_linux_t comm1;
    cc1101_linux_t comm2;
    int rc = cc1101_linux_open(&comm1, RPI_COMM1_SPI_DEV, RPI_COMM1_GDO0_GPIO);
    if (rc < 0) {
        printf("COMM1 open failed: %d\n", rc);
        return rc;
    }
    rc = cc1101_linux_open(&comm2, RPI_COMM2_SPI_DEV, RPI_COMM2_GDO0_GPIO);
    if (rc < 0) {
        printf("COMM2 open failed: %d\n", rc);
        cc1101_linux_close(&comm1);
        return rc;
    }

    print_part_info("COMM1", &comm1);
    print_part_info("COMM2", &comm2);

    cc1101_linux_reset(&comm1);
    cc1101_linux_reset(&comm2);
    rc = cc1101_linux_apply_rf_preset(&comm1);
    if (rc < 0) {
        printf("COMM1 RF preset failed: %d\n", rc);
        goto done;
    }
    rc = cc1101_linux_apply_rf_preset(&comm2);
    if (rc < 0) {
        printf("COMM2 RF preset failed: %d\n", rc);
        goto done;
    }

    printf("range test: COMM2 TX -> COMM1 RX, carrier=%.6f MHz, tx=%.1f dBm, count=%s\n",
           CC1101_CARRIER_MHZ, CC1101_TX_POWER_DBM,
           packet_count == 0 ? "infinite" : "finite");

    for (unsigned long seq = 1; packet_count == 0 || seq <= packet_count; seq++) {
        rc = cc1101_linux_start_rx(&comm1);
        if (rc != 0) {
            printf("COMM1 RX start returned: %d\n", rc);
        }

        uint8_t tx_packet[96];
        size_t tx_len = build_range_packet((uint16_t)seq, tx_packet, sizeof(tx_packet));
        if (tx_len == 0) {
            printf("range packet build failed\n");
            rc = -1;
            goto done;
        }

        rc = cc1101_linux_send_packet(&comm2, tx_packet, tx_len);
        if (rc < 0) {
            printf("COMM2 TX failed: %d\n", rc);
            goto done;
        }

        uint8_t rx_packet[96];
        uint8_t rssi = 0;
        uint8_t lqi = 0;
        int rx_len = 0;
        for (int i = 0; i < 100; i++) {
            rx_len = cc1101_linux_poll_packet(&comm1, rx_packet, sizeof(rx_packet), &rssi, &lqi);
            if (rx_len != 0) {
                break;
            }
            sleep_ms(10);
        }
        if (rx_len < 0) {
            printf("seq=%lu RX failed: %d\n", seq, rx_len);
            rc = rx_len;
            goto done;
        }
        if (rx_len == 0) {
            printf("seq=%lu RX timeout\n", seq);
            sleep_ms(500);
            continue;
        }
        if (rx_len < 2) {
            printf("seq=%lu RX too short: %d\n", seq, rx_len);
            sleep_ms(500);
            continue;
        }

        tdma_packet_t decoded;
        int decode_rc = tdma_decode_payload(&rx_packet[1], (size_t)rx_len - 1u, &decoded);
        double rssi_dbm = cc1101_rssi_dbm(rssi);
        double distance_m = radio_estimate_distance_m(rssi_dbm);
        printf("seq=%lu addr=0x%02x rssi_raw=0x%02x rssi=%.1f dBm lqi=0x%02x est_distance=%.2f m decode=%d",
               seq, rx_packet[0], rssi, rssi_dbm, lqi, distance_m, decode_rc);
        if (decode_rc == 0) {
            printf(" payload=\"%.*s\"", decoded.payload_len, (const char *)decoded.payload);
        }
        printf("\n");
        sleep_ms(500);
    }
    rc = 0;

done:
    cc1101_linux_close(&comm2);
    cc1101_linux_close(&comm1);
    return rc;
}

/**
 * @brief Raspberry gateway MVP entry point.
 *
 * Prints the configured COMM1/COMM2 devices, initializes a TDMA clock, and
 * exercises the beacon-sync path once. Later this should listen for real
 * desktop/STM32 beacons on COMM1 and transmit only in assigned slots on COMM2.
 *
 * @return 0 on the current MVP path.
 */
int main(int argc, char **argv)
{
    tdma_clock_t clock;
    tdma_clock_init(&clock);

    printf("Raspberry gateway: COMM1=%s GDO0=%d, COMM2=%s GDO0=%d\n",
           RPI_COMM1_SPI_DEV, RPI_COMM1_GDO0_GPIO,
           RPI_COMM2_SPI_DEV, RPI_COMM2_GDO0_GPIO);

    /* COMM1 listens for desktop/STM32 beacon. COMM2 transmits only in assigned slots. */
    tdma_clock_sync_beacon(&clock, 0, monotonic_us());
    printf("TDMA locked=%u frame=%u slot0_start=%lld\n",
           clock.locked, clock.frame_no, (long long)tdma_slot_start_us(&clock, 0));
    unsigned long packet_count = (argc > 1) ? strtoul(argv[1], 0, 10) : 0;
    return run_packet_test(packet_count);
}
