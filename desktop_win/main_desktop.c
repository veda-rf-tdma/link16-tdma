#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "config.h"
#include "protocol.h"
#include "radio_metrics.h"
#include "stm32_bridge_link.h"
#include "tdma.h"
#include "node_table.h"

/* Desktop master dynamic frame period auto-tuning variables */
static uint32_t desktop_frame_period_us = TDMA_FRAME_PERIOD_US;
static uint32_t desktop_slot_us = TDMA_SLOT_US;
static uint32_t desktop_tuner_rx_packets = 0;
static uint32_t desktop_last_stable_period = TDMA_FRAME_PERIOD_US;
static uint8_t desktop_active_mask = 0x0F;
typedef enum {
    D_TUNING_SWEEP,
    D_TUNING_LOCKED
} desktop_tuning_state_t;
static desktop_tuning_state_t desktop_tuner_state = D_TUNING_SWEEP;

static void sleep_ms(unsigned long ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, 0);
#endif
}

static void format_timestamp(char *out, size_t out_len)
{
    time_t now = time(0);
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    strftime(out, out_len, "%Y-%m-%d %H:%M:%S", &tm_now);
}

/**
 * @brief Build a TDMA master beacon payload for one frame.
 *
 * The beacon advertises the frame period, slot length, guard time, and slot
 * table version. It is later wrapped as a CC1101 variable-length packet.
 *
 * @param frame_no TDMA frame number to advertise.
 * @param out Destination buffer for encoded TDMA payload bytes.
 * @param out_len Size of @p out.
 * @return Encoded TDMA payload length, or 0 on failure.
 */
static size_t build_beacon(uint16_t frame_no, uint8_t *out, size_t out_len)
{
    tdma_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.network_id = TDMA_NETWORK_ID;
    pkt.type = TDMA_PKT_BEACON;
    pkt.src = TDMA_MASTER_ADDR;
    pkt.dst = TDMA_ADDR_BROADCAST;
    pkt.frame_no = frame_no;
    pkt.slot_no = 0;

    tdma_beacon_payload_t payload;
    payload.frame_no = frame_no;
    payload.frame_period_us = desktop_frame_period_us;
    payload.slot_us = desktop_slot_us;
    payload.guard_us = TDMA_GUARD_US;
    payload.slot_table_version = desktop_active_mask;
    payload.aircraft_x_cm = 0; /* Default 0 on desktop */
    payload.aircraft_y_cm = 0;

    pkt.payload_len = (uint8_t)tdma_encode_beacon(&payload, pkt.payload, sizeof(pkt.payload));
    return tdma_encode_payload(&pkt, out, out_len);
}

static void log_event(FILE *log, const char *event, uint16_t frame_no, const char *detail)
{
    char ts[32];
    uint8_t channel = tdma_channel_for_slot(frame_no, TDMA_SLOT_MASTER_BEACON);
    format_timestamp(ts, sizeof(ts));
    printf("%s frame=%u %s\n", event, frame_no, detail ? detail : "");
    if (log) {
        fprintf(log, "%s,%s,%u,0x%02x,%s,0x%02x,%s,%u,%s,%u,,,,%s\n",
                ts, event, frame_no,
                TDMA_ADDR_DESKTOP, tdma_node_role_name(tdma_node_role(TDMA_ADDR_DESKTOP)),
                TDMA_MASTER_ADDR, tdma_node_role_name(tdma_node_role(TDMA_MASTER_ADDR)),
                TDMA_SLOT_MASTER_BEACON,
                tdma_slot_role_name(tdma_slot_role(TDMA_SLOT_MASTER_BEACON)),
                channel, detail ? detail : "");
        fflush(log);
    }
}

static int send_beacon(stm32_bridge_link_t *bridge, uint16_t frame_no)
{
    uint8_t payload[96];
    uint8_t cc1101_frame[100];
    size_t payload_len = build_beacon(frame_no, payload, sizeof(payload));
    if (payload_len == 0) {
        return -1;
    }
    size_t radio_len = cc1101_wrap_variable_packet(TDMA_ADDR_BROADCAST, payload,
                                                   payload_len, cc1101_frame,
                                                   sizeof(cc1101_frame));
    if (radio_len == 0) {
        return -2;
    }
    int rc = bridge_send_packet(bridge, cc1101_frame, radio_len);
    return rc < 0 ? rc : 0;
}

static uint8_t allocate_address(tdma_node_table_t *table, uint8_t *slot_out)
{
    if (!node_table_find(table, TDMA_ADDR_BASE_2)) {
        *slot_out = TDMA_SLOT_ANCHOR_1_REPORT;
        return TDMA_ADDR_BASE_2;
    }
    if (!node_table_find(table, TDMA_ADDR_BASE_3)) {
        *slot_out = TDMA_SLOT_ANCHOR_2_REPORT;
        return TDMA_ADDR_BASE_3;
    }
    *slot_out = 0xff;
    return 0;
}

static void poll_and_log_rx(stm32_bridge_link_t *bridge, FILE *log, uint16_t frame_no, tdma_node_table_t *table)
{
    uint8_t radio_frame[100];
    uint8_t rssi = 0;
    uint8_t lqi = 0;
    int n = bridge_poll_packet_meta(bridge, radio_frame, sizeof(radio_frame), &rssi, &lqi);
    if (n <= 0) {
        return;
    }
    if (n < 2) {
        log_event(log, "RX_SHORT", frame_no, "radio frame too short");
        return;
    }

    tdma_packet_t pkt;
    int rc = tdma_decode_payload(&radio_frame[1], (size_t)n - 1u, &pkt);
    if (rc != 0) {
        char detail[80];
        snprintf(detail, sizeof(detail), "addr=0x%02x bytes=%d decode=%d",
                 radio_frame[0], n, rc);
        log_event(log, "RX_BAD", frame_no, detail);
        return;
    }

    double rssi_dbm = cc1101_rssi_dbm(rssi);
    double distance_m = radio_estimate_distance_m(rssi_dbm);
    tdma_node_role_t receiver_role = tdma_node_role(TDMA_MASTER_ADDR);
    tdma_node_role_t tx_role = tdma_node_role(pkt.src);
    tdma_slot_role_t slot_role = tdma_slot_role(pkt.slot_no);
    uint8_t channel = tdma_channel_for_slot(pkt.frame_no, pkt.slot_no);
    char detail[160];
    snprintf(detail, sizeof(detail),
             "addr=0x%02x type=%u src=0x%02x dst=0x%02x packet_frame=%u slot=%u payload_len=%u rssi_raw=0x%02x rssi=%.1f dBm lqi=0x%02x est_distance=%.2f m",
             radio_frame[0], pkt.type, pkt.src, pkt.dst, pkt.frame_no, pkt.slot_no,
             pkt.payload_len, rssi, rssi_dbm, lqi, distance_m);
    printf("RX_OK frame=%u %s\n", frame_no, detail);
    if (log) {
        char ts[32];
        format_timestamp(ts, sizeof(ts));
        fprintf(log, "%s,RX_OK,%u,0x%02x,%s,0x%02x,%s,%u,%s,%u,0x%02x,%.1f,0x%02x,%s\n",
                ts, frame_no,
                TDMA_MASTER_ADDR, tdma_node_role_name(receiver_role),
                pkt.src, tdma_node_role_name(tx_role),
                pkt.slot_no, tdma_slot_role_name(slot_role),
                channel, rssi, rssi_dbm, lqi, detail);
        fflush(log);
    }

    if (pkt.type == TDMA_PKT_JOIN_REQUEST) {
        tdma_join_request_payload_t req;
        if (tdma_decode_join_request(pkt.payload, pkt.payload_len, &req) == 0) {
            char uid_hex[25];
            for (int j = 0; j < 12; j++) {
                snprintf(&uid_hex[j * 2], 3, "%02x", req.uid[j]);
            }
            uid_hex[24] = '\0';

            uint8_t assigned_addr = 0;
            uint8_t assigned_slot = 0xff;
            tdma_node_t *existing = node_table_find_by_uid(table, req.uid);
            if (existing) {
                existing->frames_since_seen = 0;
                existing->state = TDMA_NODE_STATE_ACTIVE;
                assigned_addr = existing->node_id;
                assigned_slot = existing->slot_no;
                printf("[MASTER] Rejoin request from UID %s: Resending Address: 0x%02x, Slot: %u\n",
                       uid_hex, assigned_addr, assigned_slot);
            } else {
                assigned_addr = allocate_address(table, &assigned_slot);
                if (assigned_addr != 0) {
                    tdma_node_t *new_node = node_table_upsert(table, assigned_addr);
                    if (new_node) {
                        new_node->has_uid = 1;
                        memcpy(new_node->uid, req.uid, 12);
                        new_node->slot_no = assigned_slot;
                        new_node->state = TDMA_NODE_STATE_ACTIVE;
                        new_node->frames_since_seen = 0;
                        printf("[MASTER] New join request from UID %s: Assigned Address: 0x%02x, Slot: %u\n",
                               uid_hex, assigned_addr, assigned_slot);
                    }
                } else {
                    printf("[MASTER] New join request from UID %s REJECTED: no available address/slot\n", uid_hex);
                }
            }

            if (assigned_addr != 0) {
                tdma_packet_t accept_pkt;
                memset(&accept_pkt, 0, sizeof(accept_pkt));
                accept_pkt.network_id = TDMA_NETWORK_ID;
                accept_pkt.type = TDMA_PKT_JOIN_ACCEPT;
                accept_pkt.src = TDMA_MASTER_ADDR;
                accept_pkt.dst = TDMA_ADDR_BROADCAST;
                accept_pkt.frame_no = frame_no;
                accept_pkt.slot_no = TDMA_SLOT_JOIN;

                tdma_join_accept_payload_t accept_payload;
                memcpy(accept_payload.uid, req.uid, 12);
                accept_payload.assigned_addr = assigned_addr;
                accept_payload.assigned_slot = assigned_slot;

                accept_pkt.payload_len = (uint8_t)tdma_encode_join_accept(&accept_payload, accept_pkt.payload, sizeof(accept_pkt.payload));

                uint8_t tx_payload[96];
                uint8_t tx_radio_frame[100];
                size_t tx_payload_len = tdma_encode_payload(&accept_pkt, tx_payload, sizeof(tx_payload));
                size_t tx_radio_len = cc1101_wrap_variable_packet(TDMA_ADDR_BROADCAST, tx_payload, tx_payload_len, tx_radio_frame, sizeof(tx_radio_frame));

                int send_rc = bridge_send_packet(bridge, tx_radio_frame, tx_radio_len);
                if (send_rc == 0) {
                    log_event(log, "TX_JOIN_ACCEPT", frame_no, "sent");
                } else {
                    printf("[MASTER] Failed to send JOIN_ACCEPT: rc=%d\n", send_rc);
                }
            }
        }
    } else {
        tdma_node_t *sender = node_table_find(table, pkt.src);
        if (sender) {
            sender->frames_since_seen = 0;
            if (sender->state == TDMA_NODE_STATE_SUSPECT || sender->state == TDMA_NODE_STATE_LOST) {
                sender->state = TDMA_NODE_STATE_ACTIVE;
                printf("[MASTER] Node 0x%02x recovered to ACTIVE\n", pkt.src);
            }
        }
        if (pkt.type == TDMA_PKT_DATA && pkt.src == TDMA_AIRCRAFT_ADDR) {
            desktop_tuner_rx_packets++;
            tdma_aircraft_data_payload_t air_data;
            if (tdma_decode_aircraft_data(pkt.payload, pkt.payload_len, &air_data) == 0) {
                printf("[ECHO] Received coordinate feedback from aircraft: X=%d cm, Y=%d cm\n",
                       air_data.echoed_x_cm, air_data.echoed_y_cm);
            }
        }
        if (pkt.type == TDMA_PKT_ANCHOR_REPORT) {
            tdma_anchor_report_payload_t report;
            if (tdma_decode_anchor_report(pkt.payload, pkt.payload_len, &report) == 0) {
                if (report.has_relayed_data) {
                    printf("[RELAY] Received aircraft coordinates relayed by Anchor 0x%02x: X=%d cm, Y=%d cm\n",
                           pkt.src, report.relayed_data.echoed_x_cm, report.relayed_data.echoed_y_cm);
                }
            }
        }
    }
}

/**
 * @brief Windows desktop master MVP entry point.
 *
 * Opens the STM32 USB CDC bridge, starts RX mode, periodically sends TDMA
 * beacons, polls received radio packets, and writes a small CSV log.
 *
 * @param argc Argument count.
 * @param argv Argument values. argv[1] may specify the COM port, default COM3.
 * @return 0 on success, non-zero on bridge open failure.
 */
int main(int argc, char **argv)
{
    const char *port = (argc > 1) ? argv[1] : "COM3";
    unsigned long frame_count = (argc > 2) ? strtoul(argv[2], 0, 10) : 100;
    const char *log_path = (argc > 3) ? argv[3] : "desktop_master_log.csv";
    stm32_bridge_link_t bridge;
    FILE *log = fopen(log_path, "a");

    tdma_node_table_t master_node_table;
    node_table_init(&master_node_table);
    // Add master itself so it's not timed out or reassigned
    tdma_node_t *master_node = node_table_upsert(&master_node_table, TDMA_MASTER_ADDR);
    if (master_node) {
        master_node->slot_no = TDMA_SLOT_MASTER_BEACON;
        master_node->state = TDMA_NODE_STATE_ACTIVE;
    }

    if (bridge_open(&bridge, port) != 0) {
        fprintf(stderr, "failed to open STM32 bridge on %s\n", port);
        if (log) {
            fclose(log);
        }
        return 1;
    }
    if (log) {
        fprintf(log, "timestamp,event,frame,receiver_id,receiver_role,tx_node_id,tx_role,slot_no,slot_role,channel,rssi_raw,rssi_dbm,lqi,detail\n");
    }

    if (bridge_start_rx(&bridge) != 0) {
        fprintf(stderr, "failed to start STM32 bridge RX\n");
        bridge_close(&bridge);
        if (log) {
            fclose(log);
        }
        return 2;
    }

    printf("desktop master started: port=%s frames=%lu log=%s\n", port, frame_count, log_path);
    for (unsigned long i = 0; i < frame_count; i++) {
        uint16_t frame_no = (uint16_t)i;
        
        // Tick timeouts for dynamic nodes
        node_table_tick_timeouts(&master_node_table);

        /* 1. Evaluate liveness and rebuild desktop_active_mask */
        uint8_t next_mask = 0x03; /* Master & Aircraft are always assumed active */
        tdma_node_t *n1 = node_table_find(&master_node_table, TDMA_ADDR_BASE_2);
        if (n1 && n1->state == TDMA_NODE_STATE_ACTIVE) {
            next_mask |= (1 << 2);
        }
        tdma_node_t *n2 = node_table_find(&master_node_table, TDMA_ADDR_BASE_3);
        if (n2 && n2->state == TDMA_NODE_STATE_ACTIVE) {
            next_mask |= (1 << 3);
        }
        desktop_active_mask = next_mask;

        /* Calculate total slots and min_limit */
        uint32_t active_nodes_count = 0;
        for (int b = 0; b < 8; b++) {
            if (desktop_active_mask & (1 << b)) active_nodes_count++;
        }
        uint32_t total_slots = active_nodes_count + 1;
        uint32_t min_limit = total_slots * desktop_slot_us;

        /* 2. Run PDR Auto-tuner every 100 frames */
        if (i > 0 && i % 100 == 0) {
            double pdr = (double)desktop_tuner_rx_packets / 100.0;
            if (desktop_tuner_state == D_TUNING_SWEEP) {
                if (pdr >= 0.95) {
                    if (desktop_frame_period_us > min_limit) {
                        desktop_last_stable_period = desktop_frame_period_us;
                        desktop_frame_period_us -= 10000u;
                        printf("[TUNER] PDR=%.2f: stable, shrinking frame period to %lu us\n", pdr, (unsigned long)desktop_frame_period_us);
                    } else {
                        if (desktop_slot_us > 5000u) { /* 5ms limit */
                            desktop_slot_us -= 1000u;
                            desktop_frame_period_us = total_slots * desktop_slot_us;
                            printf("[TUNER] PDR=%.2f: shrinking slot width to %lu us\n", pdr, (unsigned long)desktop_slot_us);
                        } else {
                            desktop_tuner_state = D_TUNING_LOCKED;
                            printf("[TUNER] PDR=%.2f: Reached absolute physical limits. Locked settings.\n", pdr);
                        }
                    }
                } else if (pdr < 0.90) {
                    if (desktop_slot_us < 10000u) {
                        desktop_slot_us = 10000u;
                        desktop_frame_period_us = total_slots * desktop_slot_us;
                        printf("[TUNER] PDR=%.2f: Unstable, rollbacked slot width to 10ms\n", pdr);
                    } else if (desktop_frame_period_us < TDMA_FRAME_PERIOD_US) {
                        desktop_frame_period_us += 10000u;
                        desktop_last_stable_period = desktop_frame_period_us;
                        printf("[TUNER] PDR=%.2f: dropped, expanding frame period to %lu us\n", pdr, (unsigned long)desktop_frame_period_us);
                    } else {
                        desktop_tuner_state = D_TUNING_LOCKED;
                        printf("[TUNER] PDR=%.2f: At maximum limit, locking period\n", pdr);
                    }
                } else {
                    desktop_tuner_state = D_TUNING_LOCKED;
                    printf("[TUNER] PDR=%.2f: Settled in marginal zone, locking settings\n", pdr);
                }
            }
            desktop_tuner_rx_packets = 0;
        }

        int rc = send_beacon(&bridge, frame_no);
        if (rc == 0) {
            log_event(log, "TX_BEACON", frame_no, "sent");
        } else {
            char detail[40];
            snprintf(detail, sizeof(detail), "send failed rc=%d", rc);
            log_event(log, "TX_FAIL", frame_no, detail);
        }

        unsigned long poll_sleep = (desktop_frame_period_us / 1000u) / 10u;
        if (poll_sleep == 0) poll_sleep = 1;

        for (int poll = 0; poll < 10; poll++) {
            poll_and_log_rx(&bridge, log, frame_no, &master_node_table);
            sleep_ms(poll_sleep);
        }
    }

    bridge_close(&bridge);
    if (log) {
        fclose(log);
    }
    return 0;
}
