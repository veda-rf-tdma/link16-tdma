#include <stdio.h>
#include <string.h>
#include "node_table.h"

void node_table_init(tdma_node_table_t *table)
{
    for (uint8_t i = 0; i < TDMA_MAX_NODES; i++) {
        table->nodes[i].node_id = 0;
        table->nodes[i].slot_no = 0xffu;
        table->nodes[i].active = 0;
        table->nodes[i].last_frame_seen = 0;
        table->nodes[i].last_rssi_dbm = 0;
        table->nodes[i].last_lqi = 0;
        table->nodes[i].state = TDMA_NODE_STATE_FREE;
        table->nodes[i].has_uid = 0;
        memset(table->nodes[i].uid, 0, 12);
        table->nodes[i].frames_since_seen = 0;
    }
}

tdma_node_t *node_table_find(tdma_node_table_t *table, uint8_t node_id)
{
    for (uint8_t i = 0; i < TDMA_MAX_NODES; i++) {
        if (table->nodes[i].state != TDMA_NODE_STATE_FREE && table->nodes[i].node_id == node_id) {
            return &table->nodes[i];
        }
    }
    return 0;
}

tdma_node_t *node_table_upsert(tdma_node_table_t *table, uint8_t node_id)
{
    tdma_node_t *node = node_table_find(table, node_id);
    if (node) {
        return node;
    }
    for (uint8_t i = 0; i < TDMA_MAX_NODES; i++) {
        if (table->nodes[i].state == TDMA_NODE_STATE_FREE) {
            table->nodes[i].state = TDMA_NODE_STATE_ACTIVE;
            table->nodes[i].active = 1;
            table->nodes[i].node_id = node_id;
            table->nodes[i].slot_no = 0xffu;
            table->nodes[i].has_uid = 0;
            table->nodes[i].frames_since_seen = 0;
            return &table->nodes[i];
        }
    }
    return 0;
}

tdma_node_t *node_table_find_by_uid(tdma_node_table_t *table, const uint8_t *uid)
{
    for (uint8_t i = 0; i < TDMA_MAX_NODES; i++) {
        if (table->nodes[i].state != TDMA_NODE_STATE_FREE && table->nodes[i].has_uid) {
            int match = 1;
            for (int j = 0; j < 12; j++) {
                if (table->nodes[i].uid[j] != uid[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                return &table->nodes[i];
            }
        }
    }
    return 0;
}

void node_table_tick_timeouts(tdma_node_table_t *table)
{
    for (uint8_t i = 0; i < TDMA_MAX_NODES; i++) {
        tdma_node_t *node = &table->nodes[i];
        if (node->state == TDMA_NODE_STATE_FREE) {
            continue;
        }
        if (node->node_id == TDMA_MASTER_ADDR) {
            continue;
        }

        node->frames_since_seen++;

        if (node->state == TDMA_NODE_STATE_ACTIVE) {
            if (node->frames_since_seen >= TDMA_ANCHOR_SUSPECT_TIMEOUT_FRAMES) {
                node->state = TDMA_NODE_STATE_SUSPECT;
                printf("[MASTER] Node 0x%02x: State transition to SUSPECT (no packet for %u frames)\n",
                       node->node_id, node->frames_since_seen);
            }
        } else if (node->state == TDMA_NODE_STATE_SUSPECT) {
            if (node->frames_since_seen >= TDMA_ANCHOR_LOST_TIMEOUT_FRAMES) {
                node->state = TDMA_NODE_STATE_LOST;
                printf("[MASTER] Node 0x%02x: State transition to LOST (no packet for %u frames)\n",
                       node->node_id, node->frames_since_seen);
            }
        } else if (node->state == TDMA_NODE_STATE_LOST) {
            if (node->frames_since_seen >= TDMA_ANCHOR_GRACE_PERIOD_FRAMES) {
                printf("[MASTER] Node 0x%02x: Slot %u reclaimed (grace period of %u frames expired)\n",
                       node->node_id, node->slot_no, TDMA_ANCHOR_GRACE_PERIOD_FRAMES);
                node->state = TDMA_NODE_STATE_FREE;
                node->active = 0;
                node->node_id = 0;
                node->slot_no = 0xffu;
                node->has_uid = 0;
            }
        }
    }
}

void node_table_assign_fixed_slots(tdma_node_table_t *table)
{
    uint8_t next_slot = 2;
    for (uint8_t i = 0; i < TDMA_MAX_NODES; i++) {
        if (table->nodes[i].state != TDMA_NODE_STATE_FREE) {
            table->nodes[i].slot_no = next_slot++;
        }
    }
}
