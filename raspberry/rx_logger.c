#define _POSIX_C_SOURCE 199309L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cc1101_linux.h"
#include "config.h"
#include "protocol.h"
#include "radio_metrics.h"
#include "tdma.h"

enum {
    NODE_PROFILE_MASTER = 1u,
    NODE_PROFILE_ANCHOR_1 = 2u,
    NODE_PROFILE_ANCHOR_2 = 3u
};

typedef struct {
    unsigned profile_no;
    uint8_t receiver_id;
    const char *csv_path;
    const char *label;
} rx_logger_profile_t;

static const rx_logger_profile_t RX_LOGGER_PROFILES[] = {
    {NODE_PROFILE_MASTER, TDMA_MASTER_ADDR, "rssi_log_master.csv", "master"},
    {NODE_PROFILE_ANCHOR_1, TDMA_ANCHOR_1_ADDR, "rssi_log_anchor1.csv", "anchor1"},
    {NODE_PROFILE_ANCHOR_2, TDMA_ANCHOR_2_ADDR, "rssi_log_anchor2.csv", "anchor2"}
};

static void sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

static void format_timestamp(char *out, size_t out_len)
{
    struct timespec ts;
    struct tm tm_now;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_now);
    strftime(out, out_len, "%Y-%m-%d %H:%M:%S", &tm_now);
    size_t len = strlen(out);
    if (len + 5u < out_len) {
        snprintf(out + len, out_len - len, ".%03ld", ts.tv_nsec / 1000000L);
    }
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *out, size_t out_len)
{
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2u < out_len; i++) {
        out[pos++] = hex[data[i] >> 4];
        out[pos++] = hex[data[i] & 0x0fu];
    }
    if (out_len > 0) {
        out[pos < out_len ? pos : out_len - 1u] = '\0';
    }
}

static void bytes_to_text(const uint8_t *data, size_t len, char *out, size_t out_len)
{
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 1u < out_len; i++) {
        if (data[i] == '"') {
            out[pos++] = '\'';
        } else {
            out[pos++] = isprint(data[i]) ? (char)data[i] : '.';
        }
    }
    if (out_len > 0) {
        out[pos] = '\0';
    }
}

static void print_usage(const char *argv0)
{
    printf("usage: %s [node_no] [packet_count]\n", argv0);
    printf("node_no: 1=master(0x21), 2=anchor1(0x22), 3=anchor2(0x23)\n");
    printf("packet_count=0 or omitted means infinite\n");
    printf("auto csv: 1=rssi_log_master.csv, 2=rssi_log_anchor1.csv, 3=rssi_log_anchor2.csv\n");
    printf("COMM1 RX uses %s, GDO0 GPIO%d, shared CC1101 RF preset CHANNR=0x05\n",
           RPI_COMM1_SPI_DEV, RPI_COMM1_GDO0_GPIO);
}

static const rx_logger_profile_t *find_profile(unsigned profile_no)
{
    for (size_t i = 0; i < sizeof(RX_LOGGER_PROFILES) / sizeof(RX_LOGGER_PROFILES[0]); i++) {
        if (RX_LOGGER_PROFILES[i].profile_no == profile_no) {
            return &RX_LOGGER_PROFILES[i];
        }
    }
    return &RX_LOGGER_PROFILES[0];
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    unsigned profile_no = (argc > 1) ? strtoul(argv[1], 0, 0) : NODE_PROFILE_MASTER;
    unsigned long packet_count = (argc > 2) ? strtoul(argv[2], 0, 10) : 0;
    const rx_logger_profile_t *profile = find_profile(profile_no);
    const char *csv_path = profile->csv_path;
    uint8_t receiver_id = profile->receiver_id;

    FILE *csv = fopen(csv_path, "a");
    if (!csv) {
        perror("fopen");
        return 1;
    }
    fprintf(csv, "timestamp,receiver_id,receiver_role,addr,tx_node_id,tx_role,seq,frame_no,slot_no,slot_role,channel,rssi_raw,rssi_dbm,lqi,decode,payload_text,payload_hex\n");
    fflush(csv);

    cc1101_linux_t radio;
    int rc = cc1101_linux_open(&radio, RPI_COMM1_SPI_DEV, RPI_COMM1_GDO0_GPIO);
    if (rc < 0) {
        printf("COMM1 open failed: %d\n", rc);
        fclose(csv);
        return 2;
    }

    uint8_t partnum = 0;
    uint8_t version = 0;
    rc = cc1101_linux_read_part_info(&radio, &partnum, &version);
    if (rc < 0) {
        printf("COMM1 part info read failed: %d\n", rc);
    } else {
        printf("COMM1 PARTNUM=0x%02x VERSION=0x%02x\n", partnum, version);
    }

    cc1101_linux_reset(&radio);
    rc = cc1101_linux_apply_rf_preset(&radio);
    if (rc < 0) {
        printf("COMM1 RF preset failed: %d\n", rc);
        cc1101_linux_close(&radio);
        fclose(csv);
        return 3;
    }
    rc = cc1101_linux_start_rx(&radio);
    if (rc != 0) {
        printf("COMM1 RX start returned: %d\n", rc);
    }

    printf("RSSI logger started: profile=%u(%s) spi=%s csv=%s count=%s receiver=0x%02x(%s) carrier=%.6f MHz tx_ref=%.1f dBm\n",
           profile->profile_no, profile->label, RPI_COMM1_SPI_DEV, csv_path,
           packet_count == 0 ? "infinite" : argv[2],
           receiver_id, tdma_node_role_name(tdma_node_role(receiver_id)),
           CC1101_CARRIER_MHZ, CC1101_TX_POWER_DBM);

    for (unsigned long seen = 0; packet_count == 0 || seen < packet_count;) {
        uint8_t radio_frame[96];
        uint8_t rssi = 0;
        uint8_t lqi = 0;
        int n = cc1101_linux_poll_packet(&radio, radio_frame, sizeof(radio_frame),
                                         &rssi, &lqi);
        if (n == 0) {
            sleep_ms(10);
            continue;
        }
        if (n < 0) {
            printf("RX error: %d, restarting RX\n", n);
            cc1101_linux_start_rx(&radio);
            sleep_ms(50);
            continue;
        }
        seen++;

        char timestamp[32];
        char payload_text[80];
        char payload_hex[160];
        format_timestamp(timestamp, sizeof(timestamp));

        uint8_t addr = radio_frame[0];
        const uint8_t *payload = n > 1 ? &radio_frame[1] : 0;
        size_t payload_len = n > 1 ? (size_t)n - 1u : 0u;
        bytes_to_text(payload, payload_len, payload_text, sizeof(payload_text));
        bytes_to_hex(payload, payload_len, payload_hex, sizeof(payload_hex));

        tdma_packet_t decoded;
        int decode_rc = tdma_decode_payload(payload, payload_len, &decoded);
        unsigned node_id = decode_rc == 0 ? decoded.src : 0xffu;
        unsigned frame_no = decode_rc == 0 ? decoded.frame_no : 0xffffu;
        unsigned slot_no = decode_rc == 0 ? decoded.slot_no : 0xffu;
        unsigned seq = frame_no;
        double rssi_dbm = cc1101_rssi_dbm(rssi);
        tdma_node_role_t rx_role = tdma_node_role(receiver_id);
        tdma_node_role_t tx_role = tdma_node_role((uint8_t)node_id);
        tdma_slot_role_t slot_role = tdma_slot_role((uint8_t)slot_no);
        uint8_t channel = decode_rc == 0 ? tdma_channel_for_slot((uint16_t)frame_no, (uint8_t)slot_no) : 0xffu;

        printf("rx=%lu receiver=0x%02x(%s) tx=0x%02x(%s) slot=%u(%s) channel=%u rssi_raw=0x%02x rssi=%.1f dBm lqi=0x%02x decode=%d payload=\"%s\"\n",
               seen, receiver_id, tdma_node_role_name(rx_role),
               node_id, tdma_node_role_name(tx_role),
               slot_no, tdma_slot_role_name(slot_role), channel,
               rssi, rssi_dbm, lqi, decode_rc, payload_text);
        fprintf(csv, "%s,0x%02x,%s,0x%02x,0x%02x,%s,%u,%u,%u,%s,%u,0x%02x,%.1f,0x%02x,%d,\"%s\",%s\n",
                timestamp, receiver_id, tdma_node_role_name(rx_role), addr,
                node_id, tdma_node_role_name(tx_role), seq, frame_no, slot_no,
                tdma_slot_role_name(slot_role), channel, rssi, rssi_dbm, lqi,
                decode_rc, payload_text, payload_hex);
        fflush(csv);

        cc1101_linux_start_rx(&radio);
    }

    cc1101_linux_close(&radio);
    fclose(csv);
    return 0;
}
