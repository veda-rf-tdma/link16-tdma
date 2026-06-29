#ifndef TDMA_RUNTIME_H
#define TDMA_RUNTIME_H

#include <stdint.h>
#include "tdma.h"

typedef enum {
    TDMA_RUNTIME_IDLE = 0,
    TDMA_RUNTIME_SLOT_START,
    TDMA_RUNTIME_SWITCH_CHANNEL,
    TDMA_RUNTIME_TX_ACTIVE,
    TDMA_RUNTIME_RX_ACTIVE,
    TDMA_RUNTIME_RX_DONE,
    TDMA_RUNTIME_TX_DONE,
    TDMA_RUNTIME_SETTLING,
    TDMA_RUNTIME_GUARD,
    TDMA_RUNTIME_SLOT_DONE
} tdma_runtime_state_t;

typedef enum {
    TDMA_ACTION_NONE = 0,
    TDMA_ACTION_WAIT,
    TDMA_ACTION_SET_CHANNEL,
    TDMA_ACTION_START_TX,
    TDMA_ACTION_START_RX,
    TDMA_ACTION_STOP_RADIO,
    TDMA_ACTION_LOG_RX,
    TDMA_ACTION_SLOT_DONE
} tdma_runtime_action_t;

typedef struct {
    tdma_runtime_action_t action;
    tdma_runtime_state_t state;
    tdma_slot_plan_t plan;
    uint16_t frame_no;
    uint8_t slot_no;
    uint32_t slot_pos_us;
    uint8_t is_tx_node;
} tdma_runtime_step_t;

typedef struct {
    uint8_t local_node_id;
    uint16_t frame_no;
    uint8_t slot_no;
    uint8_t slot_valid;
    tdma_runtime_state_t state;
    tdma_slot_plan_t plan;
    uint8_t channel_set;
    uint8_t radio_started;
    volatile uint8_t gdo0_pending;
} tdma_runtime_t;

void tdma_runtime_init(tdma_runtime_t *runtime, uint8_t local_node_id);

void tdma_runtime_set_node(tdma_runtime_t *runtime, uint8_t local_node_id);

void tdma_runtime_on_gdo0_edge(tdma_runtime_t *runtime);

tdma_runtime_step_t tdma_runtime_tick(tdma_runtime_t *runtime,
                                      const tdma_clock_t *clock,
                                      int64_t now_us,
                                      double current_margin_db);

const char *tdma_runtime_state_name(tdma_runtime_state_t state);

const char *tdma_runtime_action_name(tdma_runtime_action_t action);

#endif
