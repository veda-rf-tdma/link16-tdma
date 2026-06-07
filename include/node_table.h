#ifndef NODE_TABLE_H
#define NODE_TABLE_H

#include <stdint.h>
#include "config.h"

typedef enum {
    TDMA_NODE_STATE_FREE = 0,
    TDMA_NODE_STATE_ACTIVE,
    TDMA_NODE_STATE_SUSPECT,
    TDMA_NODE_STATE_LOST
} tdma_node_state_t;

typedef struct {
    uint8_t node_id;
    uint8_t slot_no;
    uint8_t active;
    uint16_t last_frame_seen;
    int last_rssi_dbm;
    uint8_t last_lqi;
    tdma_node_state_t state;
    uint8_t uid[12];
    uint8_t has_uid;
    uint32_t frames_since_seen;
} tdma_node_t;

typedef struct {
    tdma_node_t nodes[TDMA_MAX_NODES];
} tdma_node_table_t;

tdma_node_t *node_table_find_by_uid(tdma_node_table_t *table, const uint8_t *uid);
void node_table_tick_timeouts(tdma_node_table_t *table);

/**
 * @brief Clear all node records and mark the table empty.
 *
 * @param table Node table to initialize.
 */
void node_table_init(tdma_node_table_t *table);

/**
 * @brief Find an active node record by node ID.
 *
 * @param table Node table to search.
 * @param node_id Node ID to find.
 * @return Pointer to the record, or NULL if absent.
 */
tdma_node_t *node_table_find(tdma_node_table_t *table, uint8_t node_id);

/**
 * @brief Return an existing node record or create one in the first free slot.
 *
 * @param table Node table to update.
 * @param node_id Node ID to find or add.
 * @return Pointer to the record, or NULL if the table is full.
 */
tdma_node_t *node_table_upsert(tdma_node_table_t *table, uint8_t node_id);

/**
 * @brief Assign simple fixed slots to all active nodes.
 *
 * Slots start at 2 so slot 0 can remain beacon-oriented and slot 1 can remain
 * available for control or future use.
 *
 * @param table Node table whose active nodes will be assigned.
 */
void node_table_assign_fixed_slots(tdma_node_table_t *table);

#endif
