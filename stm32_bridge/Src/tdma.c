#include "tdma.h"
#include "config.h"

void tdma_clock_init(tdma_clock_t *clock)
{
    clock->frame_period_us = TDMA_FRAME_PERIOD_US;
    clock->slot_us = TDMA_SLOT_US;
    clock->guard_us = TDMA_GUARD_US;
    clock->frame_no = 0;
    clock->frame_start_local_us = 0;
    clock->sync_offset_us = 0;
    clock->locked = 0;
    clock->active_mask = 0x0F;
}

void tdma_clock_sync_beacon(tdma_clock_t *clock, uint16_t frame_no, int64_t beacon_rx_us)
{
    if (!clock->locked) {
        clock->frame_start_local_us = beacon_rx_us;
        clock->sync_offset_us = 0;
        clock->frame_no = frame_no;
        clock->locked = 1;
        return;
    }

    int64_t expected = clock->frame_start_local_us + (int64_t)clock->frame_period_us;
    int64_t error = beacon_rx_us - expected;
    clock->sync_offset_us += error / 8;
    clock->frame_start_local_us = beacon_rx_us - clock->sync_offset_us;
    clock->frame_no = frame_no;
}

int64_t tdma_slot_start_us(const tdma_clock_t *clock, uint8_t slot_no)
{
    return clock->frame_start_local_us + ((int64_t)slot_no * (int64_t)clock->slot_us);
}

uint8_t tdma_slot_at(const tdma_clock_t *clock, int64_t now_us)
{
    if (!clock->locked || now_us < clock->frame_start_local_us) {
        return 0xffu;
    }
    int64_t elapsed = (now_us - clock->frame_start_local_us) % clock->frame_period_us;
    return (uint8_t)(elapsed / clock->slot_us);
}

int tdma_is_inside_guard(const tdma_clock_t *clock, int64_t now_us)
{
    if (!clock->locked || now_us < clock->frame_start_local_us) {
        return 1;
    }
    int64_t elapsed = (now_us - clock->frame_start_local_us) % clock->frame_period_us;
    int64_t slot_pos = elapsed % clock->slot_us;
    return slot_pos >= ((int64_t)clock->slot_us - (int64_t)clock->guard_us);
}

tdma_node_role_t tdma_node_role(uint8_t node_id)
{
    switch (node_id) {
    case TDMA_MASTER_ADDR:
        return TDMA_NODE_MASTER;
    case TDMA_AIRCRAFT_ADDR:
        return TDMA_NODE_AIRCRAFT;
    case TDMA_ANCHOR_1_ADDR:
        return TDMA_NODE_ANCHOR_1;
    case TDMA_ANCHOR_2_ADDR:
        return TDMA_NODE_ANCHOR_2;

    case TDMA_ADDR_DESKTOP:
        return TDMA_NODE_DESKTOP;
    case TDMA_ADDR_UNASSIGNED:
        return TDMA_NODE_UNASSIGNED;
    default:
        return TDMA_NODE_UNKNOWN;
    }
}

const char *tdma_node_role_name(tdma_node_role_t role)
{
    switch (role) {
    case TDMA_NODE_MASTER:
        return "MASTER";
    case TDMA_NODE_AIRCRAFT:
        return "AIRCRAFT";
    case TDMA_NODE_ANCHOR_1:
        return "ANCHOR_1";
    case TDMA_NODE_ANCHOR_2:
        return "ANCHOR_2";

    case TDMA_NODE_DESKTOP:
        return "DESKTOP";
    case TDMA_NODE_UNASSIGNED:
        return "UNASSIGNED";
    default:
        return "UNKNOWN";
    }
}

tdma_slot_role_t tdma_slot_role(uint8_t slot_no)
{
    switch (slot_no) {
    case TDMA_SLOT_MASTER_BEACON:
        return TDMA_SLOT_MASTER_BEACON_ROLE;
    case TDMA_SLOT_AIRCRAFT_TX:
        return TDMA_SLOT_AIRCRAFT_TX_ROLE;
    case TDMA_SLOT_ANCHOR_1_REPORT:
        return TDMA_SLOT_ANCHOR_1_REPORT_ROLE;
    case TDMA_SLOT_ANCHOR_2_REPORT:
        return TDMA_SLOT_ANCHOR_2_REPORT_ROLE;
    case TDMA_SLOT_JOIN:
        return TDMA_SLOT_JOIN_ROLE;
    default:
        return TDMA_SLOT_UNUSED;
    }
}

const char *tdma_slot_role_name(tdma_slot_role_t role)
{
    switch (role) {
    case TDMA_SLOT_MASTER_BEACON_ROLE:
        return "MASTER_BEACON";
    case TDMA_SLOT_AIRCRAFT_TX_ROLE:
        return "AIRCRAFT_TX";
    case TDMA_SLOT_ANCHOR_1_REPORT_ROLE:
        return "ANCHOR_1_REPORT";
    case TDMA_SLOT_ANCHOR_2_REPORT_ROLE:
        return "ANCHOR_2_REPORT";
    case TDMA_SLOT_JOIN_ROLE:
        return "JOIN_SLOT";
    default:
        return "UNUSED";
    }
}

uint8_t tdma_slot_tx_node(uint8_t slot_no)
{
    switch (slot_no) {
    case TDMA_SLOT_MASTER_BEACON:
        return TDMA_MASTER_ADDR;
    case TDMA_SLOT_AIRCRAFT_TX:
        return TDMA_AIRCRAFT_ADDR;
    case TDMA_SLOT_ANCHOR_1_REPORT:
        return TDMA_ANCHOR_1_ADDR;
    case TDMA_SLOT_ANCHOR_2_REPORT:
        return TDMA_ANCHOR_2_ADDR;
    case TDMA_SLOT_JOIN:
        return TDMA_ADDR_UNASSIGNED;
    default:
        return TDMA_ADDR_BROADCAST;
    }
}

uint8_t tdma_channel_for_slot(uint16_t frame_no, uint8_t slot_no)
{
    uint32_t seed = ((uint32_t)frame_no * FHSS_LCG_A) +
                    ((uint32_t)slot_no * FHSS_LCG_C);
    return (uint8_t)(FHSS_CHANNEL_BASE + (seed % FHSS_CHANNEL_COUNT));
}

double tdma_tx_power_for_margin(double current_margin_db)
{
    double power = CC1101_TX_POWER_DBM;
    if (current_margin_db > ATPC_TARGET_MARGIN_DB + ATPC_STEP_DB) {
        power -= ATPC_STEP_DB;
    } else if (current_margin_db < ATPC_TARGET_MARGIN_DB - ATPC_STEP_DB) {
        power += ATPC_STEP_DB;
    }
    if (power < ATPC_MIN_TX_POWER_DBM) {
        return ATPC_MIN_TX_POWER_DBM;
    }
    if (power > ATPC_MAX_TX_POWER_DBM) {
        return ATPC_MAX_TX_POWER_DBM;
    }
    return power;
}

tdma_slot_plan_t tdma_plan_for_slot(uint16_t frame_no, uint8_t slot_no,
                                    double current_margin_db)
{
    tdma_slot_plan_t plan;
    plan.slot_no = slot_no;
    plan.slot_role = tdma_slot_role(slot_no);
    plan.tx_node_id = tdma_slot_tx_node(slot_no);
    plan.tx_role = tdma_node_role(plan.tx_node_id);
    plan.channel = tdma_channel_for_slot(frame_no, slot_no);
    plan.tx_power_dbm = tdma_tx_power_for_margin(current_margin_db);
    return plan;
}
