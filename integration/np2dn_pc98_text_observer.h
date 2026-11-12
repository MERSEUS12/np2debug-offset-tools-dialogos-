#ifndef NP2DN_PC98_TEXT_OBSERVER_H
#define NP2DN_PC98_TEXT_OBSERVER_H

/*
 * PC-98 text-render observation.
 *
 * This module records only data consumed by the native text renderer and
 * successful emulated composition calls. It does not inspect or mutate CPU
 * state, VRAM, TRAM, or renderer control flow.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP2DN_PC98_TEXT_CELL_COUNT 0x1000u
#define NP2DN_PC98_TEXT_PLANE_MASK 0x04u

enum {
    NP2DN_PC98_TEXT_CELL_KANJI_CONTINUATION = 0x01u
};

/*
 * character_low/high are the raw bytes consumed by the renderer. They remain
 * available for continuation cells; flags identify their interpretation.
 */
typedef struct NP2DN_PC98TextCell {
    uint8_t valid;
    uint8_t columns;
    uint8_t flags;
    uint8_t character_low;
    uint8_t character_high;
    uint8_t attribute;
    uint16_t screen_y;
    uint16_t screen_column;
    uint32_t generation;
} NP2DN_PC98TextCell;

typedef struct NP2DN_PC98TextObserverStatus {
    uint32_t text_generation;
    uint32_t geometry_generation;
    uint32_t composition_generation;
    uint32_t host_present_generation;
    uint32_t last_host_presented_composition_generation;
    uint32_t valid_cell_count;
    uint8_t last_composition_planes;
    uint8_t last_composition_included_text;
    uint8_t last_host_presented_text;
} NP2DN_PC98TextObserverStatus;

/* Invalidate all cached cell observations. Safe on the emulator thread. */
void np2dn_pc98_text_observer_reset(void);
void np2dn_pc98_text_observer_begin_full_redraw(void);
void np2dn_pc98_text_observer_invalidate_all(void);

/* Record a cell only when maketext/maketext40 actually consumes its row. */
void np2dn_pc98_text_observer_record_cell(
    uint16_t tram_index, uint16_t screen_y, uint16_t screen_column,
    uint8_t columns, uint8_t character_low, uint8_t character_high,
    uint8_t attribute, uint8_t flags);

/* Record only after a real composition function has been invoked. */
void np2dn_pc98_text_observer_record_composition(uint8_t selected_planes);

/* Record only after the host surface blit succeeds. */
void np2dn_pc98_text_observer_record_host_presented(int success);

/* Read-only snapshot access; index is a TRAM cell index in [0, 0xFFF]. */
int np2dn_pc98_text_observer_get_cell(uint16_t tram_index,
                                      NP2DN_PC98TextCell *cell);
void np2dn_pc98_text_observer_get_status(
    NP2DN_PC98TextObserverStatus *status);

#ifdef __cplusplus
}
#endif

#endif
