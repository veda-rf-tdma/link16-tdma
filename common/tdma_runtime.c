#include <string.h>
#include "config.h"
#include "tdma_runtime.h"

static tdma_runtime_step_t make_step(const tdma_runtime_t *runtime,
                                     tdma_runtime_action_t action,
                                     uint32_t slot_pos_us)
{
    tdma_runtime_step_t step;
    step.action = action;
    step.state = runtime->state;
    step.plan = runtime->plan;
    step.frame_no = runtime->frame_no;
    step.slot_no = runtime->slot_no;
    step.slot_pos_us = slot_pos_us;
    step.is_tx_node = runtime->plan.tx_node_id == runtime->local_node_id;
    return step;
}

static void enter_slot(tdma_runtime_t *runtime, const tdma_clock_t *clock, uint16_t frame_no, uint8_t slot_no,
                       double current_margin_db)
{
    runtime->frame_no = frame_no;
    runtime->slot_no = slot_no;
    runtime->slot_valid = 1;
    runtime->state = TDMA_RUNTIME_SLOT_START;
    runtime->plan = tdma_plan_for_slot(frame_no, slot_no, current_margin_db);
    runtime->channel_set = 0;
    runtime->radio_started = 0;
    runtime->gdo0_pending = 0;

    /* Dynamic Slot Table Compaction: route node roles and override final slot as a JOIN slot */
    if (clock && clock->slot_us > 0) {
        uint8_t last_slot_in_frame = (uint8_t)(clock->frame_period_us / clock->slot_us) - 1;
        if (slot_no == last_slot_in_frame) {
            runtime->plan.slot_role = TDMA_SLOT_JOIN_ROLE;
            runtime->plan.tx_node_id = TDMA_ADDR_UNASSIGNED; /* Anyone can transmit in the spare slot */
            runtime->plan.tx_role = TDMA_NODE_UNASSIGNED;
        } else {
            uint8_t mask = clock->active_mask;
            if (mask > 0) {
                uint8_t active_nodes[8];
                int active_count = 0;
                
                // Bit 0: Master (0x21)
                if (mask & (1 << 0)) active_nodes[active_count++] = TDMA_MASTER_ADDR;
                // Bit 1: Aircraft (0x31)
                if (mask & (1 << 1)) active_nodes[active_count++] = TDMA_AIRCRAFT_ADDR;
                // Bit 2: Anchor 1 (0x22)
                if (mask & (1 << 2)) active_nodes[active_count++] = TDMA_ANCHOR_1_ADDR;
                // Bit 3: Anchor 2 (0x23)
                if (mask & (1 << 3)) active_nodes[active_count++] = TDMA_ANCHOR_2_ADDR;

                if (slot_no < active_count) {
                    uint8_t tx_node = active_nodes[slot_no];
                    runtime->plan.tx_node_id = tx_node;
                    runtime->plan.tx_role = tdma_node_role(tx_node);
                    
                    if (tx_node == TDMA_MASTER_ADDR) {
                        runtime->plan.slot_role = TDMA_SLOT_MASTER_BEACON_ROLE;
                    } else if (tx_node == TDMA_AIRCRAFT_ADDR) {
                        runtime->plan.slot_role = TDMA_SLOT_AIRCRAFT_TX_ROLE;
                    } else if (tx_node == TDMA_ANCHOR_1_ADDR) {
                        runtime->plan.slot_role = TDMA_SLOT_ANCHOR_1_REPORT_ROLE;
                    } else if (tx_node == TDMA_ANCHOR_2_ADDR) {
                        runtime->plan.slot_role = TDMA_SLOT_ANCHOR_2_REPORT_ROLE;
                    }
                } else {
                    runtime->plan.slot_role = TDMA_SLOT_UNUSED;
                    runtime->plan.tx_node_id = TDMA_ADDR_BROADCAST;
                    runtime->plan.tx_role = TDMA_NODE_UNKNOWN;
                }
            }
        }
    }
}

static int runtime_time(const tdma_clock_t *clock, int64_t now_us,
                        uint16_t *frame_no, uint8_t *slot_no,
                        uint32_t *slot_pos_us)
{
    if (!clock || !clock->locked || now_us < clock->frame_start_local_us) {
        return 0;
    }

    int64_t elapsed = now_us - clock->frame_start_local_us;
    uint32_t frames_elapsed = (uint32_t)(elapsed / (int64_t)clock->frame_period_us);
    uint32_t frame_pos = (uint32_t)(elapsed % (int64_t)clock->frame_period_us);
    *frame_no = (uint16_t)(clock->frame_no + frames_elapsed);
    *slot_no = (uint8_t)(frame_pos / clock->slot_us);
    *slot_pos_us = frame_pos % clock->slot_us;
    return 1;
}

void tdma_runtime_init(tdma_runtime_t *runtime, uint8_t local_node_id)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->local_node_id = local_node_id;
    runtime->slot_no = 0xffu;
    runtime->state = TDMA_RUNTIME_IDLE;
}

void tdma_runtime_set_node(tdma_runtime_t *runtime, uint8_t local_node_id)
{
    runtime->local_node_id = local_node_id;
}

void tdma_runtime_on_gdo0_edge(tdma_runtime_t *runtime)
{
    runtime->gdo0_pending = 1;
}

tdma_runtime_step_t tdma_runtime_tick(tdma_runtime_t *runtime,
                                      const tdma_clock_t *clock,
                                      int64_t now_us,
                                      double current_margin_db)
{
    uint16_t frame_no = 0;
    uint8_t slot_no = 0xffu;
    uint32_t slot_pos_us = 0;

    if (!runtime_time(clock, now_us, &frame_no, &slot_no, &slot_pos_us)) {
        runtime->state = TDMA_RUNTIME_IDLE;
        return make_step(runtime, TDMA_ACTION_WAIT, 0);
    }

    if (!runtime->slot_valid || runtime->frame_no != frame_no ||
        runtime->slot_no != slot_no) {
        enter_slot(runtime, clock, frame_no, slot_no, current_margin_db);
        runtime->channel_set = 1;
        return make_step(runtime, TDMA_ACTION_SET_CHANNEL, slot_pos_us);
    }

    if (slot_pos_us >= (uint32_t)(clock->slot_us - clock->guard_us)) {
        if (runtime->state != TDMA_RUNTIME_GUARD) {
            runtime->state = TDMA_RUNTIME_GUARD;
            runtime->radio_started = 0;
            runtime->gdo0_pending = 0;
            return make_step(runtime, TDMA_ACTION_STOP_RADIO, slot_pos_us);
        }
        return make_step(runtime, TDMA_ACTION_WAIT, slot_pos_us);
    }

    if (slot_pos_us >= TDMA_TXRX_US) {
        if (runtime->state != TDMA_RUNTIME_SETTLING) {
            runtime->state = TDMA_RUNTIME_SETTLING;
            runtime->radio_started = 0;
            runtime->gdo0_pending = 0;
            return make_step(runtime, TDMA_ACTION_STOP_RADIO, slot_pos_us);
        }
        return make_step(runtime, TDMA_ACTION_WAIT, slot_pos_us);
    }

    if (!runtime->channel_set) {
        runtime->state = TDMA_RUNTIME_SWITCH_CHANNEL;
        runtime->channel_set = 1;
        return make_step(runtime, TDMA_ACTION_SET_CHANNEL, slot_pos_us);
    }

    if (runtime->plan.slot_role == TDMA_SLOT_UNUSED) {
        runtime->state = TDMA_RUNTIME_SLOT_DONE;
        return make_step(runtime, TDMA_ACTION_WAIT, slot_pos_us);
    }

    if (!runtime->radio_started) {
        runtime->radio_started = 1;
        if (runtime->plan.slot_role == TDMA_SLOT_JOIN_ROLE) {
            if (runtime->local_node_id == TDMA_ADDR_UNASSIGNED) {
                runtime->state = TDMA_RUNTIME_TX_ACTIVE;
                return make_step(runtime, TDMA_ACTION_START_TX, slot_pos_us);
            } else if (runtime->local_node_id == TDMA_MASTER_ADDR) {
                runtime->state = TDMA_RUNTIME_RX_ACTIVE;
                return make_step(runtime, TDMA_ACTION_START_RX, slot_pos_us);
            } else {
                runtime->state = TDMA_RUNTIME_SLOT_DONE;
                return make_step(runtime, TDMA_ACTION_WAIT, slot_pos_us);
            }
        }
        if (runtime->plan.tx_node_id == runtime->local_node_id) {
            runtime->state = TDMA_RUNTIME_TX_ACTIVE;
            return make_step(runtime, TDMA_ACTION_START_TX, slot_pos_us);
        }
        runtime->state = TDMA_RUNTIME_RX_ACTIVE;
        return make_step(runtime, TDMA_ACTION_START_RX, slot_pos_us);
    }

    if (runtime->gdo0_pending) {
        runtime->gdo0_pending = 0;
        if (runtime->state == TDMA_RUNTIME_RX_ACTIVE) {
            runtime->state = TDMA_RUNTIME_RX_DONE;
            return make_step(runtime, TDMA_ACTION_LOG_RX, slot_pos_us);
        }
        if (runtime->state == TDMA_RUNTIME_TX_ACTIVE) {
            runtime->state = TDMA_RUNTIME_TX_DONE;
            return make_step(runtime, TDMA_ACTION_STOP_RADIO, slot_pos_us);
        }
    }

    return make_step(runtime, TDMA_ACTION_WAIT, slot_pos_us);
}

const char *tdma_runtime_state_name(tdma_runtime_state_t state)
{
    switch (state) {
    case TDMA_RUNTIME_IDLE:
        return "IDLE";
    case TDMA_RUNTIME_SLOT_START:
        return "SLOT_START";
    case TDMA_RUNTIME_SWITCH_CHANNEL:
        return "SWITCH_CHANNEL";
    case TDMA_RUNTIME_TX_ACTIVE:
        return "TX_ACTIVE";
    case TDMA_RUNTIME_RX_ACTIVE:
        return "RX_ACTIVE";
    case TDMA_RUNTIME_RX_DONE:
        return "RX_DONE";
    case TDMA_RUNTIME_TX_DONE:
        return "TX_DONE";
    case TDMA_RUNTIME_SETTLING:
        return "SETTLING";
    case TDMA_RUNTIME_GUARD:
        return "GUARD";
    case TDMA_RUNTIME_SLOT_DONE:
        return "SLOT_DONE";
    default:
        return "UNKNOWN";
    }
}

const char *tdma_runtime_action_name(tdma_runtime_action_t action)
{
    switch (action) {
    case TDMA_ACTION_NONE:
        return "NONE";
    case TDMA_ACTION_WAIT:
        return "WAIT";
    case TDMA_ACTION_SET_CHANNEL:
        return "SET_CHANNEL";
    case TDMA_ACTION_START_TX:
        return "START_TX";
    case TDMA_ACTION_START_RX:
        return "START_RX";
    case TDMA_ACTION_STOP_RADIO:
        return "STOP_RADIO";
    case TDMA_ACTION_LOG_RX:
        return "LOG_RX";
    case TDMA_ACTION_SLOT_DONE:
        return "SLOT_DONE";
    default:
        return "UNKNOWN";
    }
}
