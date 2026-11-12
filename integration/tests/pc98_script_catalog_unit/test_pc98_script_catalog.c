#include "np2dn_pc98_script_catalog.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        return 1; \
    } \
} while (0)

static uint32_t append_bytes(uint8_t *buffer, uint32_t offset,
                             const uint8_t *bytes, uint32_t length)
{
    memcpy(buffer + offset, bytes, length);
    return offset + length;
}

int main(void)
{
    static const uint8_t first_text[] =
        "Hola\\CRmundo\\HA\\CL\\E";
    static const uint8_t second_text[] =
        "Segundo\\HA\\CL\\E";
    static const uint8_t no_wait_text[] = "Sin espera";
    static const uint8_t invalid_marker[] =
        { 0x99, 0x18, 0x01, 'N', 'O', '\\', 'H', 'A', '\\' };
    uint8_t script[2048];
    uint8_t long_text[520];
    NP2DN_PC98ScriptDialogue entries[4];
    NP2DN_PC98ScriptDialogue limited[2];
    NP2DN_PC98ScriptCatalogStatus status;
    uint32_t length = 0;
    uint32_t first_marker;
    uint32_t second_marker;
    uint32_t third_marker;
    uint32_t fourth_marker;
    uint32_t expected_target;

    memset(script, 0, sizeof(script));
    memset(long_text, 'A', sizeof(long_text));

    script[length++] = 0x00;
    first_marker = length;
    script[length++] = 0x18;
    script[length++] = 0x01;
    length = append_bytes(script, length, first_text,
                          (uint32_t)(sizeof(first_text) - 1u));

    script[length++] = 0x01;
    second_marker = length;
    script[length++] = 0x18;
    script[length++] = 0x02;
    length = append_bytes(script, length, second_text,
                          (uint32_t)(sizeof(second_text) - 1u));

    script[length++] = 0x45;
    third_marker = length;
    script[length++] = 0x18;
    script[length++] = 0x01;
    length = append_bytes(script, length, no_wait_text,
                          (uint32_t)(sizeof(no_wait_text) - 1u));

    script[length++] = 0x00;
    fourth_marker = length;
    script[length++] = 0x18;
    script[length++] = 0x02;
    length = append_bytes(script, length, long_text,
                          (uint32_t)sizeof(long_text));
    expected_target = length + 1u;
    script[length++] = (uint8_t)'\\';
    script[length++] = (uint8_t)'H';
    script[length++] = (uint8_t)'A';
    script[length++] = (uint8_t)'\\';
    length = append_bytes(script, length, invalid_marker,
                          (uint32_t)sizeof(invalid_marker));

    CHECK(np2dn_pc98_script_catalog_build(
              script, length, entries, 4, &status),
          "parse valid bounded script");
    CHECK(status.marker_count == 4,
          "all valid markers retain their ordinal");
    CHECK(status.entry_count == 3,
          "only blocks with HA waits are jumpable");
    CHECK(status.skipped_without_wait == 1,
          "non-wait block is counted but not jumpable");
    CHECK(status.truncated_text_entries == 1,
          "long text reports truncation");
    CHECK(!status.output_truncated,
          "four-entry output buffer is sufficient");

    CHECK(entries[0].dialogue_id == 0 &&
          entries[0].block_start == first_marker &&
          entries[0].marker_type == 0x01,
          "first dialogue identity");
    CHECK(entries[0].target_esi == first_marker + 2u + 10u,
          "first target points to H after slash");
    CHECK(entries[0].text_length == 10 &&
          memcmp(entries[0].text, "Hola\nmundo", 10) == 0,
          "CR control becomes newline");

    CHECK(entries[1].dialogue_id == 1 &&
          entries[1].block_start == second_marker &&
          entries[1].target_esi > second_marker,
          "second dialogue identity and target");
    CHECK(entries[2].dialogue_id == 3 &&
          entries[2].block_start == fourth_marker &&
          entries[2].target_esi == expected_target,
          "dialogue IDs preserve skipped block ordinals");
    CHECK(entries[2].text_length == NP2DN_PC98_SCRIPT_TEXT_MAX &&
          (entries[2].flags &
           NP2DN_PC98_SCRIPT_ENTRY_TEXT_TRUNCATED) != 0,
          "text buffer is bounded and flagged");

    CHECK(np2dn_pc98_script_catalog_build(
              script, length, limited, 2, &status),
          "parse with limited output capacity");
    CHECK(status.entry_count == 3 && status.output_truncated,
          "catalogue reports output truncation");
    CHECK(limited[0].dialogue_id == 0 &&
          limited[1].dialogue_id == 1,
          "limited output preserves order");

    CHECK(np2dn_pc98_script_catalog_build(
              script, first_marker + 4u, entries, 4, &status),
          "parse truncated readable range safely");
    CHECK(status.entry_count == 0 &&
          status.skipped_without_wait == 1,
          "incomplete block is not exposed as jumpable");

    printf("PC-98 script catalogue parser tests passed.\n");
    return 0;
}
