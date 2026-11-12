#include "np2dn_pc98_script_catalog.h"

#include <string.h>

static int np2dn_pc98_script_marker_at(const uint8_t *data,
                                       uint32_t length, uint32_t offset)
{
    uint8_t previous;

    if (data == NULL || offset == 0 || offset + 1u >= length) {
        return 0;
    }
    previous = data[offset - 1u];
    return data[offset] == 0x18u &&
           (data[offset + 1u] == 0x01u ||
            data[offset + 1u] == 0x02u) &&
           (previous == 0x00u || previous == 0x01u ||
            previous == 0x45u);
}

static uint32_t np2dn_pc98_script_find_marker(const uint8_t *data,
                                               uint32_t length,
                                               uint32_t start)
{
    uint32_t i;

    for (i = start; i + 1u < length; ++i) {
        if (np2dn_pc98_script_marker_at(data, length, i)) {
            return i;
        }
    }
    return length;
}

static uint32_t np2dn_pc98_script_find_wait(const uint8_t *data,
                                             uint32_t start,
                                             uint32_t end)
{
    uint32_t i;

    if (end > 0u) {
        for (i = start; i + 3u < end; ++i) {
            if (data[i] == (uint8_t)'\\' &&
                data[i + 1u] == (uint8_t)'H' &&
                data[i + 2u] == (uint8_t)'A' &&
                data[i + 3u] == (uint8_t)'\\') {
                return i;
            }
        }
    }
    return end;
}

static void np2dn_pc98_script_copy_text(
    const uint8_t *data, uint32_t start, uint32_t end,
    NP2DN_PC98ScriptDialogue *entry)
{
    uint32_t i = start;

    while (i < end) {
        uint8_t value = data[i];

        if (value == (uint8_t)'\\' && i + 2u < end &&
            data[i + 1u] == (uint8_t)'C' &&
            data[i + 2u] == (uint8_t)'R') {
            value = (uint8_t)'\n';
            i += 3u;
        }
        else if (value == (uint8_t)'\\' && i + 2u < end &&
                 data[i + 1u] >= (uint8_t)'A' &&
                 data[i + 1u] <= (uint8_t)'Z' &&
                 data[i + 2u] >= (uint8_t)'A' &&
                 data[i + 2u] <= (uint8_t)'Z') {
            i += 3u;
            continue;
        }
        else {
            ++i;
        }

        if (entry->text_length < NP2DN_PC98_SCRIPT_TEXT_MAX) {
            entry->text[entry->text_length++] = value;
        }
        else {
            entry->flags |= NP2DN_PC98_SCRIPT_ENTRY_TEXT_TRUNCATED;
        }
    }
}

int np2dn_pc98_script_catalog_build(
    const uint8_t *data, uint32_t length,
    NP2DN_PC98ScriptDialogue *entries, uint32_t capacity,
    NP2DN_PC98ScriptCatalogStatus *status)
{
    NP2DN_PC98ScriptCatalogStatus local_status;
    uint32_t offset = 1u;

    if ((data == NULL && length != 0u) ||
        (entries == NULL && capacity != 0u)) {
        return 0;
    }
    if (status == NULL) {
        status = &local_status;
    }
    memset(status, 0, sizeof(*status));

    while (offset + 1u < length) {
        uint32_t next_marker;
        uint32_t wait_offset;

        if (!np2dn_pc98_script_marker_at(data, length, offset)) {
            ++offset;
            continue;
        }

        next_marker = np2dn_pc98_script_find_marker(
            data, length, offset + 2u);
        wait_offset = np2dn_pc98_script_find_wait(
            data, offset + 2u, next_marker);

        if (wait_offset < next_marker) {
            if (status->entry_count < capacity) {
                NP2DN_PC98ScriptDialogue *entry =
                    &entries[status->entry_count];
                memset(entry, 0, sizeof(*entry));
                entry->dialogue_id = status->marker_count;
                entry->block_start = offset;
                entry->target_esi = wait_offset + 1u;
                entry->marker_type = data[offset + 1u];
                np2dn_pc98_script_copy_text(
                    data, offset + 2u, wait_offset, entry);
                if (entry->flags &
                    NP2DN_PC98_SCRIPT_ENTRY_TEXT_TRUNCATED) {
                    ++status->truncated_text_entries;
                }
            }
            else {
                status->output_truncated = 1u;
            }
            ++status->entry_count;
        }
        else {
            ++status->skipped_without_wait;
        }

        ++status->marker_count;
        offset += 2u;
    }

    return 1;
}
