#include "np2dn_pc98_text_observer.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        return 1; \
    } \
} while (0)

int main(void)
{
    NP2DN_PC98TextCell cell;
    NP2DN_PC98TextObserverStatus before;
    NP2DN_PC98TextObserverStatus after;

    memset(&cell, 0, sizeof(cell));
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));

    np2dn_pc98_text_observer_reset();
    CHECK(!np2dn_pc98_text_observer_get_cell(0x0fff, &cell),
          "reset invalidates all cells");

    np2dn_pc98_text_observer_record_cell(
        0x0fff, 32, 79, 80, 0x41, 0x00, 0xe1, 0);
    CHECK(np2dn_pc98_text_observer_get_cell(0x0fff, &cell),
          "recorded cell can be read");
    CHECK(cell.character_low == 0x41 && cell.character_high == 0x00 &&
          cell.attribute == 0xe1 && cell.columns == 80,
          "recorded cell contents");
    CHECK(cell.screen_y == 32 && cell.screen_column == 79,
          "recorded screen location");

    np2dn_pc98_text_observer_record_cell(
        0x0ffe, 32, 78, 80, 0x82, 0xa0, 0xe1,
        NP2DN_PC98_TEXT_CELL_KANJI_CONTINUATION);
    CHECK(np2dn_pc98_text_observer_get_cell(0x0ffe, &cell),
          "kanji continuation cell can be read");
    CHECK(cell.character_low == 0x82 && cell.character_high == 0xa0 &&
          (cell.flags & NP2DN_PC98_TEXT_CELL_KANJI_CONTINUATION) != 0,
          "raw continuation bytes are preserved with their flag");

    np2dn_pc98_text_observer_get_status(&before);
    np2dn_pc98_text_observer_record_cell(
        0x0fff, 32, 79, 80, 0x41, 0x00, 0xe1, 0);
    np2dn_pc98_text_observer_get_status(&after);
    CHECK(before.text_generation == after.text_generation,
          "identical observation does not advance text generation");

    np2dn_pc98_text_observer_record_cell(
        0x0fff, 32, 79, 80, 0x41, 0x00, 0xe0, 0);
    np2dn_pc98_text_observer_get_status(&after);
    CHECK(after.text_generation != before.text_generation,
          "changed attribute advances text generation");

    before = after;
    np2dn_pc98_text_observer_begin_full_redraw();
    np2dn_pc98_text_observer_get_status(&after);
    CHECK(after.valid_cell_count == 0,
          "full redraw invalidates the old cell snapshot");
    CHECK(after.geometry_generation == before.geometry_generation,
          "full redraw does not pretend screen geometry changed");

    before = after;
    np2dn_pc98_text_observer_record_composition(0x05);
    np2dn_pc98_text_observer_get_status(&after);
    CHECK(after.composition_generation ==
          before.composition_generation + 1u,
          "text plus graphics composition advances generation");
    CHECK(after.last_composition_included_text == 1u &&
          after.last_composition_planes == 0x05u,
          "composition planes recorded");

    before = after;
    np2dn_pc98_text_observer_record_composition(0x02);
    np2dn_pc98_text_observer_get_status(&after);
    CHECK(after.composition_generation == before.composition_generation,
          "graphics-only composition does not count as text composition");

    before = after;
    np2dn_pc98_text_observer_begin_full_redraw();
    np2dn_pc98_text_observer_get_status(&after);
    CHECK(after.last_composition_included_text == 0 &&
          after.last_composition_planes == 0,
          "full redraw invalidates stale composition evidence");

    before = after;
    np2dn_pc98_text_observer_record_cell(
        0x0fff, 32, 79, 80, 0x41, 0x00, 0xe1, 0);
    np2dn_pc98_text_observer_invalidate_all();
    CHECK(!np2dn_pc98_text_observer_get_cell(0x0fff, &cell),
          "full invalidation clears cached cell");
    np2dn_pc98_text_observer_get_status(&after);
    CHECK(after.valid_cell_count == 0,
          "full invalidation clears valid-cell count");
    CHECK(after.geometry_generation != before.geometry_generation,
          "geometry invalidation advances geometry generation");

    printf("PC-98 text observer tests passed.\n");
    return 0;
}
