#ifndef TDMA_H
#define TDMA_H

#include <stdint.h>

typedef struct {
    uint32_t frame_period_us;
    uint32_t slot_us;
    uint32_t guard_us;
    uint16_t frame_no;
    int64_t frame_start_local_us;
    int64_t sync_offset_us;
    uint8_t locked;
    uint8_t active_mask;
} tdma_clock_t;

typedef enum {
    TDMA_NODE_UNKNOWN = 0,
    TDMA_NODE_MASTER,
    TDMA_NODE_AIRCRAFT,
    TDMA_NODE_ANCHOR_1,
    TDMA_NODE_ANCHOR_2,
    TDMA_NODE_DESKTOP,
    TDMA_NODE_UNASSIGNED
} tdma_node_role_t;

typedef enum {
    TDMA_SLOT_UNUSED = 0,
    TDMA_SLOT_MASTER_BEACON_ROLE,
    TDMA_SLOT_AIRCRAFT_TX_ROLE,
    TDMA_SLOT_ANCHOR_1_REPORT_ROLE,
    TDMA_SLOT_ANCHOR_2_REPORT_ROLE,
    TDMA_SLOT_JOIN_ROLE
} tdma_slot_role_t;

typedef struct {
    uint8_t slot_no;
    tdma_slot_role_t slot_role;
    uint8_t tx_node_id;
    tdma_node_role_t tx_role;
    uint8_t channel;
    double tx_power_dbm;
} tdma_slot_plan_t;

/**
 * @brief Initialize a local TDMA clock before the first master beacon arrives.
 *
 * @param clock Clock state to initialize.
 */
void tdma_clock_init(tdma_clock_t *clock);

/**
 * @brief Synchronize local TDMA time from a received master beacon.
 *
 * The first beacon locks the local frame start. Later beacons apply a small
 * proportional correction so clock drift is smoothed instead of jumping hard.
 *
 * @param clock Clock state to update.
 * @param frame_no Frame number carried by the beacon.
 * @param beacon_rx_us Local monotonic receive timestamp in microseconds.
 */
void tdma_clock_sync_beacon(tdma_clock_t *clock, uint16_t frame_no, int64_t beacon_rx_us);

/**
 * @brief Return the local timestamp for the start of a slot in the current frame.
 *
 * @param clock Synchronized TDMA clock.
 * @param slot_no Slot number within the current frame.
 * @return Local monotonic timestamp in microseconds.
 */
int64_t tdma_slot_start_us(const tdma_clock_t *clock, uint8_t slot_no);

/**
 * @brief Calculate which slot contains the given local time.
 *
 * @param clock Synchronized TDMA clock.
 * @param now_us Local monotonic timestamp in microseconds.
 * @return Slot number, or 0xff if the clock is not locked.
 */
uint8_t tdma_slot_at(const tdma_clock_t *clock, int64_t now_us);

/**
 * @brief Check whether the current time is inside the slot guard interval.
 *
 * @param clock Synchronized TDMA clock.
 * @param now_us Local monotonic timestamp in microseconds.
 * @return 1 inside guard or when unlocked, 0 otherwise.
 */
int tdma_is_inside_guard(const tdma_clock_t *clock, int64_t now_us);

tdma_node_role_t tdma_node_role(uint8_t node_id);

const char *tdma_node_role_name(tdma_node_role_t role);

tdma_slot_role_t tdma_slot_role(uint8_t slot_no);

const char *tdma_slot_role_name(tdma_slot_role_t role);

uint8_t tdma_slot_tx_node(uint8_t slot_no);

uint8_t tdma_channel_for_slot(uint16_t frame_no, uint8_t slot_no);

double tdma_tx_power_for_margin(double current_margin_db);

tdma_slot_plan_t tdma_plan_for_slot(uint16_t frame_no, uint8_t slot_no,
                                    double current_margin_db);

#endif
