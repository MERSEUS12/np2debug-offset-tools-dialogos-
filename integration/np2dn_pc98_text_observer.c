#include "np2dn_pc98_text_observer.h"

#include <string.h>

static NP2DN_PC98TextCell s_cells[NP2DN_PC98_TEXT_CELL_COUNT];
static NP2DN_PC98TextObserverStatus s_status;

static void np2dn_pc98_text_next_generation(uint32_t *generation)
{
    ++(*generation);
    if (*generation == 0) {
        ++(*generation);
    }
}

void np2dn_pc98_text_observer_reset(void)
{
    memset(s_cells, 0, sizeof(s_cells));
    s_status.valid_cell_count = 0;
    np2dn_pc98_text_next_generation(&s_status.text_generation);
    np2dn_pc98_text_next_generation(&s_status.geometry_generation);
}

void np2dn_pc98_text_observer_invalidate_all(void)
{
    uint32_t i;

    for (i = 0; i < NP2DN_PC98_TEXT_CELL_COUNT; ++i) {
        s_cells[i].valid = 0;
    }
    s_status.valid_cell_count = 0;
    np2dn_pc98_text_next_generation(&s_status.text_generation);
    np2dn_pc98_text_next_generation(&s_status.geometry_generation);
}

void np2dn_pc98_text_observer_record_cell(
    uint16_t tram_index, uint16_t screen_y, uint16_t screen_column,
    uint8_t columns, uint8_t character_low, uint8_t character_high,
    uint8_t attribute, uint8_t flags)
{
    NP2DN_PC98TextCell *cell;

    if (tram_index >= NP2DN_PC98_TEXT_CELL_COUNT ||
        (columns != 40u && columns != 80u)) {
        return;
    }

    cell = &s_cells[tram_index];
    if (!cell->valid) {
        ++s_status.valid_cell_count;
    }
    if (!cell->valid || cell->screen_y != screen_y ||
        cell->screen_column != screen_column || cell->columns != columns ||
        cell->character_low != character_low ||
        cell->character_high != character_high ||
        cell->attribute != attribute || cell->flags != flags) {
        np2dn_pc98_text_next_generation(&s_status.text_generation);
    }

    cell->valid = 1;
    cell->columns = columns;
    cell->flags = flags;
    cell->character_low = character_low;
    cell->character_high = character_high;
    cell->attribute = attribute;
    cell->screen_y = screen_y;
    cell->screen_column = screen_column;
    cell->generation = s_status.text_generation;
}

void np2dn_pc98_text_observer_record_composition(uint8_t selected_planes)
{
    s_status.last_composition_planes = selected_planes;
    s_status.last_composition_included_text =
        (selected_planes & NP2DN_PC98_TEXT_PLANE_MASK) ? 1u : 0u;

    if (s_status.last_composition_included_text) {
        np2dn_pc98_text_next_generation(&s_status.composition_generation);
    }
}

int np2dn_pc98_text_observer_get_cell(uint16_t tram_index,
                                      NP2DN_PC98TextCell *cell)
{
    if (tram_index >= NP2DN_PC98_TEXT_CELL_COUNT || cell == NULL ||
        !s_cells[tram_index].valid) {
        return 0;
    }
    *cell = s_cells[tram_index];
    return 1;
}

void np2dn_pc98_text_observer_get_status(
    NP2DN_PC98TextObserverStatus *status)
{
    if (status != NULL) {
        *status = s_status;
    }
}
