#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif
#include "config.h"
#include "protocol.h"
#include "radio_metrics.h"
#include "stm32_bridge_link.h"
#include "tdma.h"
#include "node_table.h"
#include "ekf.h"

/* Keypress stubs and mapping for cross-platform fallback */
static int check_keypress(void)
{
#ifdef _WIN32
    return _kbhit();
#else
    return 0;
#endif
}

static int get_keypress(void)
{
#ifdef _WIN32
    return _getch();
#else
    return 0;
#endif
}

#include <stdarg.h>

#ifdef _WIN32
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
static void enable_ansi_support(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return;
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}
#endif

#define EVENT_LOG_SIZE 5
static char event_logs[EVENT_LOG_SIZE][128];
static int event_log_count = 0;

static void add_event_log(const char *format, ...) {
    char buf[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    // Shift logs to make room for newest at top (index 0)
    for (int i = EVENT_LOG_SIZE - 1; i > 0; i--) {
        strncpy(event_logs[i], event_logs[i-1], sizeof(event_logs[i]) - 1);
        event_logs[i][sizeof(event_logs[i]) - 1] = '\0';
    }
    strncpy(event_logs[0], buf, sizeof(event_logs[0]) - 1);
    event_logs[0][sizeof(event_logs[0]) - 1] = '\0';
    if (event_log_count < EVENT_LOG_SIZE) {
        event_log_count++;
    }
}

typedef enum {
    MODE_LIVENESS_MONITOR,
    MODE_ACTIVE_TRACKING
} desktop_mode_t;

static desktop_mode_t current_desktop_mode = MODE_LIVENESS_MONITOR;
static ekf_t desktop_ekf;
static int desktop_ekf_initialized = 0;

/* EKF intermediate RSSI caches */
static uint8_t desktop_master_last_rssi = 0;
static uint8_t desktop_anchor1_last_rssi = 0;
static uint8_t desktop_anchor2_last_rssi = 0;
static int desktop_master_has_rssi = 0;
static int desktop_anchor1_has_rssi = 0;
static int desktop_anchor2_has_rssi = 0;

static int16_t desktop_est_x_cm = 0;
static int16_t desktop_est_y_cm = 0;

/* Interactive LED toggle variables */
static int desktop_led_command_active = 0;
static int desktop_led_command_frame_counter = 0;

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

static void draw_dashboard(uint16_t frame_no, const char *port, const char *log_path, tdma_node_table_t *table)
{
    // Move cursor to top-left (flicker-free redraw)
    printf("\033[H");

    printf("======================================================================\n");
    printf("                  TDMA RADIO MASTER SYSTEM MONITOR                    \n");
    printf("======================================================================\n");
    printf(" Port: %-8s | Frame No: %-6u | Log: %s\033[K\n", port, frame_no, log_path);
    printf("======================================================================\n\n");

    printf(" [NODE STATUS]\n");
    printf("  - Master (0x21)   : [  OK  ] (Role: Beacon Master)\033[K\n");
    
    tdma_node_t *mob = node_table_find(table, TDMA_ADDR_MOBILE_1);
    const char *mob_status = (mob && mob->state == TDMA_NODE_STATE_ACTIVE) ? "ACTIVE" : "LOST  ";
    printf("  - Aircraft (0x31) : [%s] (Role: Mobile Node)\033[K\n", mob_status);

    const char *a1_status = (desktop_active_mask & (1 << 2)) ? "ACTIVE" : "LOST  ";
    printf("  - Anchor 1 (0x22) : [%s] (Role: Ranging Anchor)\033[K\n", a1_status);

    const char *a2_status = (desktop_active_mask & (1 << 3)) ? "ACTIVE" : "LOST  ";
    printf("  - Anchor 2 (0x23) : [%s] (Role: Ranging Anchor)\033[K\n\n", a2_status);

    printf(" [SYSTEM PARAMETERS]\n");
    printf("  - Frame Period    : %-7lu us (Slot Duration: %lu us)\033[K\n", 
           (unsigned long)desktop_frame_period_us, (unsigned long)desktop_slot_us);
    printf("  - EKF Mode        : %s\033[K\n\n", 
           (current_desktop_mode == MODE_ACTIVE_TRACKING) ? "ACTIVE_TRACKING" : "LIVENESS_MONITOR");

    printf(" [EKF TARGET ESTIMATION (Apollonius Seq EKF)]\n");
    if (desktop_ekf_initialized) {
        double uncertainty = sqrt(desktop_ekf.P[0][0] + desktop_ekf.P[1][1]);
        printf("  - Target Position : ( %6.2f, %6.2f ) meters\033[K\n", 
               desktop_ekf.state[0], desktop_ekf.state[1]);
        printf("  - Estimation Error: %.3f m\033[K\n\n", uncertainty);
    } else {
        printf("  - Target Position : (   N/A,   N/A ) (Wait for setup 's')\033[K\n");
        printf("  - Estimation Error: N/A\033[K\n\n");
    }

    printf(" [RECENT LOG EVENTS]\n");
    for (int i = 0; i < EVENT_LOG_SIZE; i++) {
        if (i < event_log_count) {
            printf("  > %s\033[K\n", event_logs[i]);
        } else {
            printf("  > \033[K\n");
        }
    }
    printf("\n");

    printf("======================================================================\n");
    printf("  [s] Configure anchor coordinates   [t] Inject LED command   [q] Exit\n");
    printf("======================================================================\n");
    fflush(stdout);
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
    
    uint8_t final_mask = desktop_active_mask;
    if (desktop_led_command_active) {
        final_mask |= 0x80;
        desktop_led_command_frame_counter--;
        if (desktop_led_command_frame_counter <= 0) {
            desktop_led_command_active = 0;
        }
    }
    payload.slot_table_version = final_mask;
    payload.aircraft_x_cm = desktop_est_x_cm;
    payload.aircraft_y_cm = desktop_est_y_cm;

    pkt.payload_len = (uint8_t)tdma_encode_beacon(&payload, pkt.payload, sizeof(pkt.payload));
    return tdma_encode_payload(&pkt, out, out_len);
}

static void log_event(FILE *log, const char *event, uint16_t frame_no, const char *detail)
{
    char ts[32];
    uint8_t channel = tdma_channel_for_slot(frame_no, TDMA_SLOT_MASTER_BEACON);
    format_timestamp(ts, sizeof(ts));
    add_event_log("%-14s Frame %u: %s", event, frame_no, detail ? detail : "");
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
    add_event_log("RX_OK          Frame %u: Src=0x%02x RSSI=%.1f LQI=%u", frame_no, pkt.src, rssi_dbm, lqi);
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
                add_event_log("[JOIN] Rejoin UID %s: Addr 0x%02x, Slot %u",
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
                        add_event_log("[JOIN] New join UID %s: Addr 0x%02x, Slot %u",
                                      uid_hex, assigned_addr, assigned_slot);
                    }
                } else {
                    add_event_log("[JOIN] New join UID %s REJECTED: no slots", uid_hex);
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
                    add_event_log("[JOIN] Failed to send JOIN_ACCEPT: rc=%d", send_rc);
                }
            }
        }
    } else {
        tdma_node_t *sender = node_table_find(table, pkt.src);
        if (sender) {
            sender->frames_since_seen = 0;
            if (sender->state == TDMA_NODE_STATE_SUSPECT || sender->state == TDMA_NODE_STATE_LOST) {
                sender->state = TDMA_NODE_STATE_ACTIVE;
                add_event_log("[NODE] Node 0x%02x recovered to ACTIVE", pkt.src);
            }
        }
        if (pkt.type == TDMA_PKT_DATA && pkt.src == TDMA_AIRCRAFT_ADDR) {
            desktop_tuner_rx_packets++;
            tdma_aircraft_data_payload_t air_data;
            if (tdma_decode_aircraft_data(pkt.payload, pkt.payload_len, &air_data) == 0) {
                printf("[ECHO] Received coordinate feedback from aircraft: X=%d cm, Y=%d cm\n",
                       air_data.echoed_x_cm, air_data.echoed_y_cm);
            }
            /* Cache direct measurement from aircraft */
            desktop_master_last_rssi = rssi;
            desktop_master_has_rssi = 1;
        } else if (pkt.type == TDMA_PKT_ANCHOR_REPORT) {
            tdma_anchor_report_payload_t report;
            if (tdma_decode_anchor_report(pkt.payload, pkt.payload_len, &report) == 0) {
                if (pkt.src == TDMA_ANCHOR_1_ADDR) {
                    desktop_anchor1_last_rssi = report.rssi_raw;
                    desktop_anchor1_has_rssi = 1;
                } else if (pkt.src == TDMA_ANCHOR_2_ADDR) {
                    desktop_anchor2_last_rssi = report.rssi_raw;
                    desktop_anchor2_has_rssi = 1;
                }
                if (report.has_relayed_data) {
                    printf("[RELAY] Received aircraft coordinates relayed by Anchor 0x%02x: X=%d cm, Y=%d cm\n",
                           pkt.src, report.relayed_data.echoed_x_cm, report.relayed_data.echoed_y_cm);
                }
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

#define MAX_PROFILES 32
typedef struct {
    char name[64];
    double x1, y1, x2, y2;
} anchor_profile_t;

static void save_anchor_profile(const char *name, double x1, double y1, double x2, double y2)
{
    anchor_profile_t profiles[MAX_PROFILES];
    int count = 0;

    FILE *f = fopen("anchor_profiles.txt", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f) && count < MAX_PROFILES) {
            char p_name[64];
            double px1, py1, px2, py2;
            if (sscanf(line, "%63[^,],%lf,%lf,%lf,%lf", p_name, &px1, &py1, &px2, &py2) == 5) {
                strcpy(profiles[count].name, p_name);
                profiles[count].x1 = px1;
                profiles[count].y1 = py1;
                profiles[count].x2 = px2;
                profiles[count].y2 = py2;
                count++;
            }
        }
        fclose(f);
    }

    int found_target = -1;
    int found_latest = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(profiles[i].name, name) == 0) found_target = i;
        if (strcmp(profiles[i].name, "__latest__") == 0) found_latest = i;
    }

    if (found_target != -1) {
        profiles[found_target].x1 = x1;
        profiles[found_target].y1 = y1;
        profiles[found_target].x2 = x2;
        profiles[found_target].y2 = y2;
    } else if (count < MAX_PROFILES) {
        strcpy(profiles[count].name, name);
        profiles[count].x1 = x1;
        profiles[count].y1 = y1;
        profiles[count].x2 = x2;
        profiles[count].y2 = y2;
        count++;
    }

    if (found_latest != -1) {
        profiles[found_latest].x1 = x1;
        profiles[found_latest].y1 = y1;
        profiles[found_latest].x2 = x2;
        profiles[found_latest].y2 = y2;
    } else if (count < MAX_PROFILES) {
        strcpy(profiles[count].name, "__latest__");
        profiles[count].x1 = x1;
        profiles[count].y1 = y1;
        profiles[count].x2 = x2;
        profiles[count].y2 = y2;
        count++;
    }

    f = fopen("anchor_profiles.txt", "w");
    if (f) {
        for (int i = 0; i < count; i++) {
            fprintf(f, "%s,%.3f,%.3f,%.3f,%.3f\n", 
                    profiles[i].name, 
                    profiles[i].x1, profiles[i].y1, 
                    profiles[i].x2, profiles[i].y2);
        }
        fclose(f);
    }
}

static int load_anchor_profile(const char *name, double *x1, double *y1, double *x2, double *y2)
{
    FILE *f = fopen("anchor_profiles.txt", "r");
    if (!f) return 0;

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char p_name[64];
        double px1, py1, px2, py2;
        if (sscanf(line, "%63[^,],%lf,%lf,%lf,%lf", p_name, &px1, &py1, &px2, &py2) == 5) {
            if (strcmp(p_name, name) == 0) {
                *x1 = px1;
                *y1 = py1;
                *x2 = px2;
                *y2 = py2;
                found = 1;
                break;
            }
        }
    }
    fclose(f);
    return found;
}

static void print_available_profiles(void)
{
    FILE *f = fopen("anchor_profiles.txt", "r");
    if (!f) {
        printf("  No saved profiles found.\n");
        return;
    }

    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        char p_name[64];
        double px1, py1, px2, py2;
        if (sscanf(line, "%63[^,],%lf,%lf,%lf,%lf", p_name, &px1, &py1, &px2, &py2) == 5) {
            if (strcmp(p_name, "__latest__") != 0) {
                printf("  - %s: Anchor1=(%.2f, %.2f) m, Anchor2=(%.2f, %.2f) m\n", 
                       p_name, px1, py1, px2, py2);
                count++;
            }
        }
    }
    fclose(f);
    if (count == 0) {
        printf("  No custom profiles registered yet.\n");
    }
}

static void run_desktop_ekf_cycle(uint16_t frame_no, FILE *log)
{
    if (!desktop_ekf_initialized || current_desktop_mode != MODE_ACTIVE_TRACKING) {
        return;
    }

    double dt = (double)desktop_frame_period_us / 1000000.0;
    ekf_predict(&desktop_ekf, dt);

    double alpha = 2.0;
    double p_m = 0.0, p_a1 = 0.0, p_a2 = 0.0;
    int has_m = desktop_master_has_rssi;
    int has_a1 = desktop_anchor1_has_rssi;
    int has_a2 = desktop_anchor2_has_rssi;

    if (has_m) p_m = pow(10.0, cc1101_rssi_dbm(desktop_master_last_rssi) / 10.0);
    if (has_a1) p_a1 = pow(10.0, cc1101_rssi_dbm(desktop_anchor1_last_rssi) / 10.0);
    if (has_a2) p_a2 = pow(10.0, cc1101_rssi_dbm(desktop_anchor2_last_rssi) / 10.0);

    /* EKF sequential update for available pairs */
    if (has_m && has_a1 && p_m > 0 && p_a1 > 0) {
        double z_12 = pow(p_a1 / p_m, 1.0 / alpha);
        ekf_update_apollonius(&desktop_ekf, "0x21", "0x22", z_12);
    }
    if (has_m && has_a2 && p_m > 0 && p_a2 > 0) {
        double z_13 = pow(p_a2 / p_m, 1.0 / alpha);
        ekf_update_apollonius(&desktop_ekf, "0x21", "0x23", z_13);
    }
    if (has_a1 && has_a2 && p_a1 > 0 && p_a2 > 0) {
        double z_23 = pow(p_a2 / p_a1, 1.0 / alpha);
        ekf_update_apollonius(&desktop_ekf, "0x22", "0x23", z_23);
    }

    desktop_est_x_cm = (int16_t)(desktop_ekf.state[0] * 100.0);
    desktop_est_y_cm = (int16_t)(desktop_ekf.state[1] * 100.0);
    double uncertainty = sqrt(desktop_ekf.P[0][0] + desktop_ekf.P[1][1]);
    if (log) {
        char ts[32];
        format_timestamp(ts, sizeof(ts));
        fprintf(log, "%s,EKF_EST,%u,0x%02x,MASTER,0x31,AIRCRAFT,0,UNUSED,0,0,0,0,Est=(%.4f;%.4f) Uncertainty=%.4f\n",
                ts, frame_no, TDMA_MASTER_ADDR, desktop_ekf.state[0], desktop_ekf.state[1], uncertainty);
        fflush(log);
    }

    /* Reset flags for next frame */
    desktop_master_has_rssi = 0;
    desktop_anchor1_has_rssi = 0;
    desktop_anchor2_has_rssi = 0;
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
    const char *port_arg = (argc > 1) ? argv[1] : "COM3";
    char port_buf[64];
    const char *port = port_arg;

#ifdef _WIN32
    // If the port argument is numeric (e.g. "3"), auto-prefix it with "COM"
    int is_numeric = 1;
    for (int idx = 0; port_arg[idx] != '\0'; idx++) {
        if (port_arg[idx] < '0' || port_arg[idx] > '9') {
            is_numeric = 0;
            break;
        }
    }
    if (is_numeric && port_arg[0] != '\0') {
        snprintf(port_buf, sizeof(port_buf), "COM%s", port_arg);
        port = port_buf;
    }
#endif

    unsigned long frame_count = (argc > 2) ? strtoul(argv[2], 0, 10) : 0; // default to 0 (infinite)
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

#ifdef _WIN32
    enable_ansi_support();
#endif
    // Clear screen once at startup
    printf("\033[2J\033[H");

    for (unsigned long i = 0; (frame_count == 0) || (i < frame_count); i++) {
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



        /* Check for keyboard trigger */
        if (check_keypress()) {
            int key = get_keypress();
            if (key == 't' || key == 'T') {
                desktop_led_command_active = 1;
                desktop_led_command_frame_counter = 5;
                printf("\n[LED] Toggle command injected (will transmit for 5 frames)!\n");
            }
            else if (key == 'q' || key == 'Q') {
                printf("\n[EXIT] Exiting program cleanly...\n");
                break;
            }
            else if (current_desktop_mode == MODE_LIVENESS_MONITOR && (key == 's' || key == 'S')) {
                printf("\n=========================================================\n");
                printf("[SETUP] Keyboard trigger detected. Setting up Anchor coordinates.\n");
                printf("=========================================================\n");
                printf("Select Setup Method:\n");
                printf("  1. Enter new coordinates and save profile\n");
                printf("  2. Load last used coordinates (__latest__)\n");
                printf("  3. Load coordinates by profile name\n");
                printf("Option (1-3): ");
                fflush(stdout);

                int opt = 0;
                if (scanf("%d", &opt) != 1) {
                    while (getchar() != '\n');
                    opt = 1; // default to enter new
                }
                int c;
                while ((c = getchar()) != '\n' && c != EOF);

                double x1 = 5.0, y1 = 0.0;
                double x2 = 2.5, y2 = 4.33;
                int setup_ok = 0;

                if (opt == 1) {
                    printf("\nEnter relative X Y coords for Anchor 1 (0x22) in meters (default 5.0 0.0): ");
                    fflush(stdout);
                    if (scanf("%lf %lf", &x1, &y1) != 2) {
                        while (getchar() != '\n');
                    }
                    printf("Enter relative X Y coords for Anchor 2 (0x23) in meters (default 2.5 4.33): ");
                    fflush(stdout);
                    if (scanf("%lf %lf", &x2, &y2) != 2) {
                        while (getchar() != '\n');
                    }
                    while ((c = getchar()) != '\n' && c != EOF);

                    char p_name[64];
                    printf("Enter profile name to save this configuration (e.g. lab_room_1): ");
                    fflush(stdout);
                    if (scanf("%63s", p_name) == 1) {
                        save_anchor_profile(p_name, x1, y1, x2, y2);
                        printf("[SUCCESS] Profile '%s' saved and set to __latest__.\n", p_name);
                    } else {
                        save_anchor_profile("default", x1, y1, x2, y2);
                        printf("[SUCCESS] Saved as profile 'default' and set to __latest__.\n");
                    }
                    while ((c = getchar()) != '\n' && c != EOF);
                    setup_ok = 1;
                }
                else if (opt == 2) {
                    if (load_anchor_profile("__latest__", &x1, &y1, &x2, &y2)) {
                        printf("\n[SUCCESS] Loaded last used coordinates (__latest__):\n");
                        printf("  Anchor 1 (0x22): (%.2f, %.2f) m\n", x1, y1);
                        printf("  Anchor 2 (0x23): (%.2f, %.2f) m\n", x2, y2);
                        setup_ok = 1;
                    } else {
                        printf("\n[ERROR] No last used coordinates (__latest__) found. Please enter manually.\n");
                        setup_ok = 0;
                    }
                }
                else if (opt == 3) {
                    printf("\n--- Available Profiles ---\n");
                    print_available_profiles();
                    printf("--------------------------\n");
                    char p_name[64];
                    printf("Enter profile name to load: ");
                    fflush(stdout);
                    if (scanf("%63s", p_name) == 1) {
                        if (load_anchor_profile(p_name, &x1, &y1, &x2, &y2)) {
                            printf("[SUCCESS] Loaded profile '%s':\n", p_name);
                            printf("  Anchor 1 (0x22): (%.2f, %.2f) m\n", x1, y1);
                            printf("  Anchor 2 (0x23): (%.2f, %.2f) m\n", x2, y2);
                            
                            save_anchor_profile("__latest__", x1, y1, x2, y2);
                            setup_ok = 1;
                        } else {
                            printf("[ERROR] Profile '%s' not found.\n", p_name);
                            setup_ok = 0;
                        }
                    }
                    while ((c = getchar()) != '\n' && c != EOF);
                }

                if (setup_ok) {
                    /* Initialize EKF with loaded/input coordinates */
                    ekf_init(&desktop_ekf, x1 / 2.0, y2 / 2.0);
                    ekf_set_anchor_position(&desktop_ekf, "0x21", 0.0, 0.0);
                    ekf_set_anchor_position(&desktop_ekf, "0x22", x1, y1);
                    ekf_set_anchor_position(&desktop_ekf, "0x23", x2, y2);
                    desktop_ekf_initialized = 1;
                    current_desktop_mode = MODE_ACTIVE_TRACKING;

                    add_event_log("[MODE] Active EKF tracking mode activated");
                }
            }
        }

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
                        add_event_log("[TUNER] PDR=%.2f: stable, shrinking period to %lu us", pdr, (unsigned long)desktop_frame_period_us);
                    } else {
                        if (desktop_slot_us > 5000u) { /* 5ms limit */
                            desktop_slot_us -= 1000u;
                            desktop_frame_period_us = total_slots * desktop_slot_us;
                            add_event_log("[TUNER] PDR=%.2f: shrinking slot width to %lu us", pdr, (unsigned long)desktop_slot_us);
                        } else {
                            desktop_tuner_state = D_TUNING_LOCKED;
                            add_event_log("[TUNER] PDR=%.2f: Reached physical limits. Locked.", pdr);
                        }
                    }
                } else if (pdr < 0.90) {
                    if (desktop_slot_us < 10000u) {
                        desktop_slot_us = 10000u;
                        desktop_frame_period_us = total_slots * desktop_slot_us;
                        add_event_log("[TUNER] PDR=%.2f: Unstable, rollbacked slot to 10ms", pdr);
                    } else if (desktop_frame_period_us < TDMA_FRAME_PERIOD_US) {
                        desktop_frame_period_us += 10000u;
                        desktop_last_stable_period = desktop_frame_period_us;
                        add_event_log("[TUNER] PDR=%.2f: dropped, expanding period to %lu us", pdr, (unsigned long)desktop_frame_period_us);
                    } else {
                        desktop_tuner_state = D_TUNING_LOCKED;
                        add_event_log("[TUNER] PDR=%.2f: At maximum limit, locking period", pdr);
                    }
                } else {
                    desktop_tuner_state = D_TUNING_LOCKED;
                    add_event_log("[TUNER] PDR=%.2f: Settled, locking settings", pdr);
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

        /* Run EKF cycle at the end of the frame */
        if (current_desktop_mode == MODE_ACTIVE_TRACKING) {
            run_desktop_ekf_cycle(frame_no, log);
        }

        /* Draw/Refresh the console TUI dashboard */
        draw_dashboard(frame_no, port, log_path, &master_node_table);
    }

    bridge_close(&bridge);
    if (log) {
        fclose(log);
    }
    return 0;
}
