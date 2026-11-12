#include "np2dn_pc98_dialogue_adapter.h"

#include "../win9x/compiler.h"
#include "../break.h"
#include "../i386c/cpumem.h"
#include "np2dn_dialogue_core.h"
#include "np2dn_pc98_script_catalog.h"
#include "np2dn_pc98_text_observer.h"
#include "np2dn_dos_file_tracker.h"
#include "np2dn_debug_api.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define NP2DN_PC98_LOCATOR_KIND 0x50433938u
#define NP2DN_PC98_LOCATOR_SIZE 21u
#define NP2DN_PC98_SCRIPT_BUFFER_SIZE 0x10000u
#define NP2DN_PC98_CHECKPOINT_MAGIC 0x50433938u
#define NP2DN_PC98_JUMP_TIMEOUT_MS 5000u

typedef struct NP2DN_PC98Checkpoint {
    uint32_t magic;
    uint16_t old_segment;
    uint16_t target_segment;
    uint32_t old_esi;
    uint32_t old_block_start;
    uint32_t target_esi;
    uint32_t target_block_start;
    uint32_t jump_count_before;
    uint32_t rollback_count_before;
    uint32_t start_tick;
    uint32_t rollback_start_tick;
    uint64_t composition_generation_before;
    uint32_t host_present_generation_before;
    uint32_t text_generation_before;
    uint64_t rollback_composition_generation_before;
    uint32_t rollback_host_present_generation_before;
    uint32_t rollback_text_generation_before;
    uint8_t target_visible_before;
    uint8_t apply_started;
    uint8_t rollback_started;
    uint8_t reserved;
} NP2DN_PC98Checkpoint;

static uint8_t s_script_data[NP2DN_PC98_SCRIPT_BUFFER_SIZE];
static NP2DN_PC98ScriptDialogue
    s_script_entries[NP2DN_DIALOGUE_CATALOG_MAX];
static NP2DN_DialogueCatalogEntry
    s_catalog_entries[NP2DN_DIALOGUE_CATALOG_MAX];
static uint32_t s_catalog_count;
static uint32_t s_segment;
static uint16_t s_base_offset;
static uint32_t s_script_length;
static char s_script_name[NP2DN_DOS_FILE_NAME_MAX];
static uint64_t s_catalog_generation;
static uint8_t s_catalog_truncated;
static uint8_t s_catalog_valid;
static uint32_t s_text_codepage = 437u;
static NP2DN_PC98Checkpoint s_active_checkpoint;

static int np2dn_pc98_emulator_paused(void)
{
    NP2DN_DebugStatus status;
    memset(&status, 0, sizeof(status));
    np2dn_debug_get_status(&status);
    return status.stop_emulation != 0;
}

static void np2dn_pc98_copy_string(char *destination, size_t capacity,
                                   const char *source)
{
    size_t i;

    if (destination == NULL || capacity == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    for (i = 0; i + 1u < capacity && source[i] != '\0'; ++i) {
        destination[i] = source[i];
    }
    destination[i] = '\0';
}

static void np2dn_pc98_write_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xffu);
    destination[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void np2dn_pc98_write_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xffu);
    destination[1] = (uint8_t)((value >> 8) & 0xffu);
    destination[2] = (uint8_t)((value >> 16) & 0xffu);
    destination[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void np2dn_pc98_write_u64(uint8_t *destination, uint64_t value)
{
    np2dn_pc98_write_u32(destination, (uint32_t)value);
    np2dn_pc98_write_u32(destination + 4, (uint32_t)(value >> 32));
}

static uint16_t np2dn_pc98_read_u16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8));
}

static uint32_t np2dn_pc98_read_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static uint64_t np2dn_pc98_read_u64(const uint8_t *source)
{
    return (uint64_t)np2dn_pc98_read_u32(source) |
           ((uint64_t)np2dn_pc98_read_u32(source + 4) << 32);
}

static uint64_t np2dn_pc98_hash_bytes(uint64_t hash,
                                      const uint8_t *data,
                                      uint32_t length)
{
    uint32_t i;

    for (i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t np2dn_pc98_script_hash(uint16_t segment,
                                       uint16_t base_offset,
                                       uint32_t length,
                                       const char *name)
{
    uint64_t hash = 1469598103934665603ULL;
    uint32_t i;

    hash ^= segment;
    hash *= 1099511628211ULL;
    hash ^= base_offset;
    hash *= 1099511628211ULL;
    hash ^= length;
    hash *= 1099511628211ULL;
    for (i = 0; name[i] != '\0'; ++i) {
        hash ^= (uint8_t)name[i];
        hash *= 1099511628211ULL;
    }
    return np2dn_pc98_hash_bytes(hash, s_script_data, length);
}

static int np2dn_pc98_convert_text(const uint8_t *source,
                                   uint16_t source_length,
                                   char *destination,
                                   uint32_t destination_capacity)
{
    WCHAR wide[NP2DN_PC98_SCRIPT_TEXT_MAX + 1u];
    int wide_length;
    int utf8_length;
    int units;

    if (destination == NULL || destination_capacity == 0) {
        return 0;
    }
    destination[0] = '\0';
    if (source_length == 0) {
        return 1;
    }

    wide_length = MultiByteToWideChar(
        s_text_codepage, 0, (LPCCH)source, source_length,
        wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_length <= 0) {
        return 0;
    }

    units = wide_length;
    utf8_length = 0;
    while (units > 0) {
        utf8_length = WideCharToMultiByte(
            CP_UTF8, 0, wide, units, destination,
            (int)(destination_capacity - 1u), NULL, NULL);
        if (utf8_length > 0) {
            break;
        }
        --units;
    }
    if (utf8_length <= 0) {
        return 0;
    }
    destination[utf8_length] = '\0';
    return units == wide_length;
}

static int np2dn_pc98_build_catalog(int force)
{
    uint16_t segment = np2track_getseg();
    uint16_t base_offset = 0;
    uint32_t length = 0;
    char name[NP2DN_DOS_FILE_NAME_MAX];
    uint64_t generation;
    NP2DN_PC98ScriptCatalogStatus status;
    uint32_t i;

    memset(name, 0, sizeof(name));
    if (!np2track_loaded_script_info(segment, &base_offset,
                                     &length, name, sizeof(name)) ||
        length == 0 ||
        length > NP2DN_PC98_SCRIPT_BUFFER_SIZE ||
        (uint32_t)base_offset + length > 0x10000u) {
        s_catalog_valid = 0;
        return 0;
    }

    if (!force && s_catalog_valid && s_segment == segment &&
        s_base_offset == base_offset && s_script_length == length &&
        strcmp(s_script_name, name) == 0) {
        return 1;
    }

    for (i = 0; i < length; ++i) {
        s_script_data[i] = memp_read8(
            ((uint32_t)segment << 4) + base_offset + i);
    }
    generation = np2dn_pc98_script_hash(segment, base_offset, length, name);
    if (generation == 0) {
        generation = 1;
    }
    if (s_catalog_valid && s_catalog_generation == generation &&
        s_segment == segment && s_base_offset == base_offset &&
        s_script_length == length && strcmp(s_script_name, name) == 0) {
        return 1;
    }

    memset(s_script_entries, 0, sizeof(s_script_entries));
    memset(s_catalog_entries, 0, sizeof(s_catalog_entries));
    memset(&status, 0, sizeof(status));
    if (!np2dn_pc98_script_catalog_build(
            s_script_data, length, s_script_entries,
            NP2DN_DIALOGUE_CATALOG_MAX, &status)) {
        s_catalog_valid = 0;
        return 0;
    }

    s_catalog_count = status.entry_count;
    if (s_catalog_count > NP2DN_DIALOGUE_CATALOG_MAX) {
        s_catalog_count = NP2DN_DIALOGUE_CATALOG_MAX;
    }
    s_catalog_truncated = status.output_truncated;

    for (i = 0; i < s_catalog_count; ++i) {
        NP2DN_PC98ScriptDialogue *parsed = &s_script_entries[i];
        NP2DN_DialogueCatalogEntry *entry = &s_catalog_entries[i];
        uint32_t target_esi = (uint32_t)base_offset + parsed->target_esi;
        uint32_t block_start = (uint32_t)base_offset + parsed->block_start;
        int text_complete;

        memset(entry, 0, sizeof(*entry));
        snprintf(entry->dialogue_id, sizeof(entry->dialogue_id),
                 "%s#%lu", name, (unsigned long)parsed->dialogue_id);
        np2dn_pc98_copy_string(entry->scene_id,
                               sizeof(entry->scene_id), name);
        entry->speaker[0] = '\0';
        entry->locator_kind = NP2DN_PC98_LOCATOR_KIND;
        entry->locator_size = NP2DN_PC98_LOCATOR_SIZE;
        np2dn_pc98_write_u16(entry->locator, segment);
        np2dn_pc98_write_u16(entry->locator + 2, base_offset);
        np2dn_pc98_write_u32(entry->locator + 4, target_esi);
        np2dn_pc98_write_u32(entry->locator + 8, block_start);
        entry->locator[12] = parsed->marker_type;
        np2dn_pc98_write_u64(entry->locator + 13, generation);

        text_complete = np2dn_pc98_convert_text(
            parsed->text, parsed->text_length,
            entry->text, sizeof(entry->text));
        if (!text_complete ||
            (parsed->flags & NP2DN_PC98_SCRIPT_ENTRY_TEXT_TRUNCATED)) {
            entry->flags |= NP2DN_DIALOGUE_CATALOG_ENTRY_TEXT_TRUNCATED;
        }
    }

    s_segment = segment;
    s_base_offset = base_offset;
    s_script_length = length;
    np2dn_pc98_copy_string(s_script_name, sizeof(s_script_name), name);
    s_catalog_generation = generation;
    s_catalog_valid = 1;
    return 1;
}

static int np2dn_pc98_find_catalog_entry(const char *dialogue_id)
{
    uint32_t i;

    if (dialogue_id == NULL) {
        return -1;
    }
    for (i = 0; i < s_catalog_count; ++i) {
        if (strcmp(s_catalog_entries[i].dialogue_id, dialogue_id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int np2dn_pc98_find_ordinal(int dialogue_id)
{
    uint32_t i;

    if (dialogue_id < 0) {
        return -1;
    }
    for (i = 0; i < s_catalog_count; ++i) {
        if (s_script_entries[i].dialogue_id == (uint32_t)dialogue_id) {
            return (int)i;
        }
    }
    return -1;
}

static int np2dn_pc98_decode_locator(const NP2DN_DialogueTarget *target,
                                     uint16_t *segment,
                                     uint16_t *base_offset,
                                     uint32_t *target_esi,
                                     uint32_t *block_start,
                                     uint8_t *marker_type,
                                     uint64_t *generation)
{
    if (target == NULL ||
        target->locator_kind != NP2DN_PC98_LOCATOR_KIND ||
        target->locator_size != NP2DN_PC98_LOCATOR_SIZE) {
        return 0;
    }
    if (segment) {
        *segment = np2dn_pc98_read_u16(target->locator);
    }
    if (base_offset) {
        *base_offset = np2dn_pc98_read_u16(target->locator + 2);
    }
    if (target_esi) {
        *target_esi = np2dn_pc98_read_u32(target->locator + 4);
    }
    if (block_start) {
        *block_start = np2dn_pc98_read_u32(target->locator + 8);
    }
    if (marker_type) {
        *marker_type = target->locator[12];
    }
    if (generation) {
        *generation = np2dn_pc98_read_u64(target->locator + 13);
    }
    return 1;
}

static int np2dn_pc98_observer_contains_text(const uint8_t *expected,
                                              uint16_t expected_length)
{
    uint8_t visible_cells[512][80];
    uint8_t valid_cells[512][80];
    uint8_t visible[8192];
    uint8_t normalized_expected[NP2DN_PC98_SCRIPT_TEXT_MAX + 1u];
    uint8_t normalized_visible[8192];
    uint32_t visible_length = 0;
    uint32_t expected_length_normalized = 0;
    uint32_t normalized_visible_length = 0;
    uint32_t index;
    uint32_t y;
    uint32_t i;
    int pending_space = 0;

    if (expected == NULL || expected_length == 0 ||
        expected_length > NP2DN_PC98_SCRIPT_TEXT_MAX) {
        return 0;
    }

    memset(visible_cells, 0, sizeof(visible_cells));
    memset(valid_cells, 0, sizeof(valid_cells));

    for (index = 0; index < NP2DN_PC98_TEXT_CELL_COUNT; ++index) {
        NP2DN_PC98TextCell cell;
        if (np2dn_pc98_text_observer_get_cell((uint16_t)index, &cell) &&
            cell.screen_y < 512u && cell.screen_column < 80u) {
            visible_cells[cell.screen_y][cell.screen_column] =
                cell.character_low;
            valid_cells[cell.screen_y][cell.screen_column] = 1;
        }
    }

    for (y = 0; y < 512u; ++y) {
        int last_column = -1;
        uint32_t column;

        for (column = 0; column < 80u; ++column) {
            if (valid_cells[y][column] &&
                visible_cells[y][column] != 0 &&
                visible_cells[y][column] != (uint8_t)' ') {
                last_column = (int)column;
            }
        }
        if (last_column < 0) {
            continue;
        }
        for (column = 0; column <= (uint32_t)last_column; ++column) {
            if (valid_cells[y][column] && visible_length < sizeof(visible)) {
                uint8_t value = visible_cells[y][column];
                visible[visible_length++] = value ? value : (uint8_t)' ';
            }
        }
        if (visible_length < sizeof(visible)) {
            visible[visible_length++] = (uint8_t)' ';
        }
    }

    for (i = 0; i < expected_length; ++i) {
        uint8_t value = expected[i];
        if (value == (uint8_t)'\\' && i + 2u < expected_length &&
            expected[i + 1u] >= (uint8_t)'A' &&
            expected[i + 1u] <= (uint8_t)'Z' &&
            expected[i + 2u] >= (uint8_t)'A' &&
            expected[i + 2u] <= (uint8_t)'Z') {
            if (expected[i + 1u] == (uint8_t)'C' &&
                expected[i + 2u] == (uint8_t)'R') {
                value = (uint8_t)' ';
            }
            else {
                i += 2u;
                continue;
            }
            i += 2u;
        }
        if (value == (uint8_t)'\n' || value == (uint8_t)'\r' ||
            value == (uint8_t)'\t' || value == (uint8_t)' ') {
            if (expected_length_normalized != 0) {
                pending_space = 1;
            }
        }
        else {
            if (pending_space &&
                expected_length_normalized < sizeof(normalized_expected)) {
                normalized_expected[expected_length_normalized++] =
                    (uint8_t)' ';
            }
            pending_space = 0;
            if (expected_length_normalized < sizeof(normalized_expected)) {
                normalized_expected[expected_length_normalized++] = value;
            }
        }
    }

    pending_space = 0;
    for (i = 0; i < visible_length; ++i) {
        uint8_t value = visible[i];
        if (value == (uint8_t)'\n' || value == (uint8_t)'\r' ||
            value == (uint8_t)'\t' || value == (uint8_t)' ') {
            if (normalized_visible_length != 0) {
                pending_space = 1;
            }
        }
        else {
            if (pending_space &&
                normalized_visible_length < sizeof(normalized_visible)) {
                normalized_visible[normalized_visible_length++] =
                    (uint8_t)' ';
            }
            pending_space = 0;
            if (normalized_visible_length < sizeof(normalized_visible)) {
                normalized_visible[normalized_visible_length++] = value;
            }
        }
    }

    if (expected_length_normalized == 0 ||
        expected_length_normalized > normalized_visible_length) {
        return 0;
    }
    for (i = 0;
         i + expected_length_normalized <= normalized_visible_length;
         ++i) {
        if (memcmp(normalized_visible + i, normalized_expected,
                   expected_length_normalized) == 0) {
            return 1;
        }
    }
    return 0;
}

static int np2dn_pc98_observe(void *context,
                              NP2DN_DialogueSnapshot *snapshot)
{
    uint32_t esi;
    uint32_t block_start;
    int dialogue_id;
    char text[NP2DN_PC98_SCRIPT_TEXT_MAX];
    int entry_index;

    (void)context;
    if (snapshot == NULL || !np2dn_pc98_build_catalog(0) ||
        !np2track_current(&esi, &dialogue_id, &block_start,
                          text, sizeof(text))) {
        return 0;
    }
    entry_index = np2dn_pc98_find_ordinal(dialogue_id);
    if (entry_index < 0) {
        return 0;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    np2dn_pc98_copy_string(snapshot->dialogue_id,
                           sizeof(snapshot->dialogue_id),
                           s_catalog_entries[entry_index].dialogue_id);
    np2dn_pc98_copy_string(snapshot->scene_id,
                           sizeof(snapshot->scene_id), s_script_name);
    snapshot->generation = s_catalog_generation;
    return 1;
}

static int np2dn_pc98_enumerate(
    void *context, uint32_t offset, uint32_t capacity,
    NP2DN_DialogueCatalogEntry *entries, uint32_t *returned,
    uint32_t *total, uint64_t *generation)
{
    uint32_t i;
    uint32_t available;

    (void)context;
    if (entries == NULL || returned == NULL || total == NULL ||
        generation == NULL || capacity == 0 ||
        !np2dn_pc98_build_catalog(1)) {
        return 0;
    }

    *total = s_catalog_truncated
        ? NP2DN_DIALOGUE_CATALOG_MAX + 1u : s_catalog_count;
    *generation = s_catalog_generation;
    *returned = 0;
    if (offset >= s_catalog_count) {
        return 1;
    }

    available = s_catalog_count - offset;
    if (available > capacity) {
        available = capacity;
    }
    for (i = 0; i < available; ++i) {
        entries[i] = s_catalog_entries[offset + i];
    }
    *returned = available;
    return 1;
}

static int np2dn_pc98_resolve(void *context, const char *dialogue_id,
                              NP2DN_DialogueTarget *target)
{
    int index;
    const NP2DN_DialogueCatalogEntry *entry;

    (void)context;
    if (target == NULL || !np2dn_pc98_build_catalog(0)) {
        return 0;
    }
    index = np2dn_pc98_find_catalog_entry(dialogue_id);
    if (index < 0) {
        return 0;
    }

    entry = &s_catalog_entries[index];
    memset(target, 0, sizeof(*target));
    np2dn_pc98_copy_string(target->dialogue_id,
                           sizeof(target->dialogue_id),
                           entry->dialogue_id);
    np2dn_pc98_copy_string(target->scene_id,
                           sizeof(target->scene_id), entry->scene_id);
    target->locator_kind = entry->locator_kind;
    target->locator_size = entry->locator_size;
    memcpy(target->locator, entry->locator, entry->locator_size);
    target->flags = entry->flags;
    target->catalog_generation = s_catalog_generation;
    return 1;
}

static int np2dn_pc98_validate(
    void *context, const NP2DN_DialogueSnapshot *current,
    const NP2DN_DialogueTarget *target)
{
    uint16_t segment;
    uint16_t base_offset;
    uint32_t target_esi;
    uint32_t block_start;
    uint8_t marker_type;
    uint64_t generation;
    uint16_t loaded_base;
    uint32_t loaded_length;
    char loaded_name[NP2DN_DOS_FILE_NAME_MAX];
    uint32_t physical;
    uint32_t script_end;
    uint8_t previous;
    int entry_index;

    (void)context;
    if (current == NULL || target == NULL ||
        !np2dn_pc98_build_catalog(1) ||
        !np2dn_pc98_decode_locator(target, &segment, &base_offset,
                                   &target_esi, &block_start,
                                   &marker_type, &generation)) {
        return 0;
    }

    entry_index = np2dn_pc98_find_catalog_entry(target->dialogue_id);
    if (entry_index < 0 ||
        (s_catalog_entries[entry_index].flags &
         NP2DN_DIALOGUE_CATALOG_ENTRY_TEXT_TRUNCATED) != 0 ||
        s_catalog_entries[entry_index].locator_size != target->locator_size ||
        memcmp(s_catalog_entries[entry_index].locator, target->locator,
               target->locator_size) != 0) {
        return 0;
    }

    if (generation != s_catalog_generation ||
        current->generation != s_catalog_generation ||
        segment != np2track_getseg() ||
        segment != s_segment || base_offset != s_base_offset ||
        strcmp(current->scene_id, target->scene_id) != 0 ||
        strcmp(target->scene_id, s_script_name) != 0) {
        return 0;
    }

    memset(loaded_name, 0, sizeof(loaded_name));
    if (!np2track_loaded_script_info(segment, &loaded_base,
                                     &loaded_length, loaded_name,
                                     sizeof(loaded_name)) ||
        loaded_base != base_offset || loaded_length != s_script_length ||
        strcmp(loaded_name, target->scene_id) != 0) {
        return 0;
    }

    script_end = (uint32_t)base_offset + loaded_length;
    if (block_start <= base_offset ||
        block_start + 1u >= script_end ||
        target_esi < block_start + 2u ||
        target_esi + 2u >= script_end) {
        return 0;
    }

    physical = ((uint32_t)segment << 4);
    previous = memp_read8(physical + block_start - 1u);
    if ((previous != 0x00u && previous != 0x01u && previous != 0x45u) ||
        memp_read8(physical + block_start) != 0x18u ||
        memp_read8(physical + block_start + 1u) != marker_type ||
        (marker_type != 0x01u && marker_type != 0x02u) ||
        memp_read8(physical + target_esi) != (UINT8)'H' ||
        memp_read8(physical + target_esi + 1u) != (UINT8)'A' ||
        memp_read8(physical + target_esi + 2u) != (UINT8)'\\') {
        return 0;
    }
    return 1;
}

static int np2dn_pc98_prepare(
    void *context, const NP2DN_DialogueSnapshot *current,
    const NP2DN_DialogueTarget *target,
    uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    NP2DN_PC98Checkpoint cp;
    NP2DN_PC98TextObserverStatus observer;
    uint32_t current_esi;
    uint32_t current_block;
    uint32_t target_block_start;
    int current_id;
    int current_index;
    int target_index;
    uint16_t target_segment;
    uint32_t target_esi;

    (void)context;
    if (current == NULL || target == NULL || checkpoint == NULL ||
        np2jump_is_armed() || !np2dn_pc98_build_catalog(0) ||
        !np2dn_pc98_decode_locator(target, &target_segment, NULL,
                                   &target_esi, &target_block_start,
                                   NULL, NULL) ||
        !np2track_current(&current_esi, &current_id, &current_block,
                          NULL, 0) ||
        target_segment != np2track_getseg() ||
        current_esi == target_esi ||
        strcmp(current->scene_id, target->scene_id) != 0 ||
        current->generation != s_catalog_generation) {
        return 0;
    }

    current_index = np2dn_pc98_find_ordinal(current_id);
    target_index = np2dn_pc98_find_catalog_entry(target->dialogue_id);
    if (current_index < 0 || target_index < 0 ||
        current_block != s_base_offset +
                         s_script_entries[current_index].block_start ||
        strcmp(current->dialogue_id,
               s_catalog_entries[current_index].dialogue_id) != 0 ||
        (s_script_entries[current_index].flags &
         NP2DN_PC98_SCRIPT_ENTRY_TEXT_TRUNCATED) != 0 ||
        (s_script_entries[target_index].flags &
         NP2DN_PC98_SCRIPT_ENTRY_TEXT_TRUNCATED) != 0) {
        return 0;
    }

    memset(&cp, 0, sizeof(cp));
    cp.magic = NP2DN_PC98_CHECKPOINT_MAGIC;
    cp.old_segment = np2track_getseg();
    cp.target_segment = target_segment;
    cp.old_esi = current_esi;
    cp.old_block_start = current_block;
    cp.target_esi = target_esi;
    cp.target_block_start = target_block_start;
    cp.jump_count_before = np2jump_count();
    cp.start_tick = GetTickCount();
    np2dn_pc98_text_observer_get_status(&observer);
    cp.composition_generation_before = observer.composition_generation;
    cp.host_present_generation_before = observer.host_present_generation;
    cp.text_generation_before = observer.text_generation;
    cp.target_visible_before = (uint8_t)np2dn_pc98_observer_contains_text(
        s_script_entries[target_index].text,
        s_script_entries[target_index].text_length);
    memset(checkpoint, 0, NP2DN_DIALOGUE_CHECKPOINT_MAX);
    memcpy(checkpoint, &cp, sizeof(cp));
    s_active_checkpoint = cp;
    return 1;
}

static NP2DN_DialogueResult np2dn_pc98_apply(
    void *context, const NP2DN_DialogueTarget *target,
    uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    NP2DN_PC98Checkpoint *cp = (NP2DN_PC98Checkpoint *)checkpoint;
    uint16_t segment;
    uint32_t target_esi;
    uint32_t target_block_start;
    uint32_t entry_esi;

    (void)context;
    if (cp == NULL || cp->magic != NP2DN_PC98_CHECKPOINT_MAGIC ||
        target == NULL ||
        !np2dn_pc98_decode_locator(target, &segment, NULL,
                                   &target_esi, &target_block_start,
                                   NULL, NULL) ||
        target_block_start > 0xfffdu ||
        target_esi <= target_block_start + 2u) {
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }

    /* Re-enter at the block header, not at its trailing HA wait token.
     * The interpreter must process the marker, render the text and reach its own wait. */
    entry_esi = target_block_start;
    cp->jump_count_before = np2jump_count();
    cp->start_tick = GetTickCount();
    cp->apply_started = 1;
    cp->rollback_started = 0;
    np2jump_arm(segment, entry_esi, NULL, 0);
    s_active_checkpoint = *cp;
    return NP2DN_DIALOGUE_RESULT_PENDING;
}

static NP2DN_DialogueResult np2dn_pc98_poll(
    void *context, const NP2DN_DialogueTarget *target,
    uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    NP2DN_PC98Checkpoint *cp = (NP2DN_PC98Checkpoint *)checkpoint;

    (void)context;
    (void)target;
    if (cp == NULL || cp->magic != NP2DN_PC98_CHECKPOINT_MAGIC) {
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }
    s_active_checkpoint = *cp;
    if (np2jump_count() != cp->jump_count_before) {
        return NP2DN_DIALOGUE_RESULT_DONE;
    }
    if (!np2jump_is_armed()) {
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }
    if (np2dn_pc98_emulator_paused()) {
        return NP2DN_DIALOGUE_RESULT_PENDING;
    }
    if ((uint32_t)(GetTickCount() - cp->start_tick) >
        NP2DN_PC98_JUMP_TIMEOUT_MS) {
        np2jump_disarm();
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }
    return NP2DN_DIALOGUE_RESULT_PENDING;
}

static NP2DN_DialogueResult np2dn_pc98_verify(
    void *context, const NP2DN_DialogueTarget *target,
    NP2DN_DialogueSnapshot *observed)
{
    NP2DN_PC98Checkpoint *cp = &s_active_checkpoint;
    NP2DN_PC98TextObserverStatus observer;
    uint16_t target_segment;
    uint32_t target_esi;
    uint32_t target_block;
    uint32_t current_esi;
    uint32_t current_block;
    int current_id;
    int entry_index;
    NP2DN_PC98ScriptDialogue *parsed;

    (void)context;
    if (target == NULL || observed == NULL ||
        cp->magic != NP2DN_PC98_CHECKPOINT_MAGIC ||
        !np2dn_pc98_decode_locator(target, &target_segment, NULL,
                                   &target_esi, &target_block, NULL, NULL)) {
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }
    if ((uint32_t)(GetTickCount() - cp->start_tick) >
            NP2DN_PC98_JUMP_TIMEOUT_MS &&
        !np2dn_pc98_emulator_paused()) {
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }
    if (np2jump_count() == cp->jump_count_before ||
        np2track_getseg() != target_segment ||
        !np2track_current(&current_esi, &current_id, &current_block,
                          NULL, 0)) {
        return NP2DN_DIALOGUE_RESULT_PENDING;
    }

    entry_index = np2dn_pc98_find_catalog_entry(target->dialogue_id);
    if (entry_index < 0) {
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }
    parsed = &s_script_entries[entry_index];
    if (current_esi != target_esi ||
        current_block != target_block ||
        current_id != (int)parsed->dialogue_id) {
        return NP2DN_DIALOGUE_RESULT_PENDING;
    }
    if ((parsed->flags & NP2DN_PC98_SCRIPT_ENTRY_TEXT_TRUNCATED) != 0 ||
        (s_catalog_entries[entry_index].flags &
         NP2DN_DIALOGUE_CATALOG_ENTRY_TEXT_TRUNCATED) != 0) {
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }

    np2dn_pc98_text_observer_get_status(&observer);
    if (observer.composition_generation <=
            cp->composition_generation_before ||
        observer.host_present_generation <=
            cp->host_present_generation_before ||
        observer.last_host_presented_text == 0 ||
        observer.last_host_presented_composition_generation <=
            cp->composition_generation_before ||
        (!cp->target_visible_before &&
         observer.text_generation <= cp->text_generation_before)) {
        return NP2DN_DIALOGUE_RESULT_PENDING;
    }

    if (!np2dn_pc98_observer_contains_text(
            parsed->text, parsed->text_length)) {
        return NP2DN_DIALOGUE_RESULT_PENDING;
    }

    memset(observed, 0, sizeof(*observed));
    np2dn_pc98_copy_string(observed->dialogue_id,
                           sizeof(observed->dialogue_id),
                           target->dialogue_id);
    np2dn_pc98_copy_string(observed->scene_id,
                           sizeof(observed->scene_id),
                           target->scene_id);
    observed->generation = s_catalog_generation;
    observed->flags = NP2DN_DIALOGUE_SNAPSHOT_STATE_VERIFIED |
                      NP2DN_DIALOGUE_SNAPSHOT_PRESENTED |
                      NP2DN_DIALOGUE_SNAPSHOT_CONTINUATION_READY;
    return NP2DN_DIALOGUE_RESULT_DONE;
}

static NP2DN_DialogueResult np2dn_pc98_rollback(
    void *context,
    uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    NP2DN_PC98Checkpoint *cp = (NP2DN_PC98Checkpoint *)checkpoint;

    (void)context;
    if (cp == NULL || cp->magic != NP2DN_PC98_CHECKPOINT_MAGIC) {
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }
    if (!cp->apply_started ||
        np2jump_count() == cp->jump_count_before) {
        np2jump_disarm();
        s_active_checkpoint = *cp;
        return NP2DN_DIALOGUE_RESULT_DONE;
    }

    if (!cp->rollback_started) {
        NP2DN_PC98TextObserverStatus observer;
        cp->rollback_started = 1;
        cp->rollback_count_before = np2jump_count();
        cp->rollback_start_tick = GetTickCount();
        np2dn_pc98_text_observer_get_status(&observer);
        cp->rollback_composition_generation_before =
            observer.composition_generation;
        cp->rollback_host_present_generation_before =
            observer.host_present_generation;
        cp->rollback_text_generation_before = observer.text_generation;
        /* Re-run the previous block so rollback restores both text and flow. */
        np2jump_arm(cp->old_segment, cp->old_block_start, NULL, 0);
        s_active_checkpoint = *cp;
        return NP2DN_DIALOGUE_RESULT_PENDING;
    }
    if (np2jump_count() != cp->rollback_count_before) {
        uint32_t restored_esi;
        uint32_t restored_block;
        int restored_id;
        if (np2track_getseg() == cp->old_segment &&
            np2track_current(&restored_esi, &restored_id,
                             &restored_block, NULL, 0) &&
            restored_esi == cp->old_esi &&
            restored_block == cp->old_block_start) {
            int restored_index = np2dn_pc98_find_ordinal(restored_id);
            NP2DN_PC98TextObserverStatus observer;
            np2dn_pc98_text_observer_get_status(&observer);
            if (restored_index >= 0 &&
                observer.composition_generation >
                    cp->rollback_composition_generation_before &&
                observer.host_present_generation >
                    cp->rollback_host_present_generation_before &&
                observer.last_host_presented_text != 0 &&
                observer.last_host_presented_composition_generation >
                    cp->rollback_composition_generation_before &&
                observer.text_generation >
                    cp->rollback_text_generation_before &&
                np2dn_pc98_observer_contains_text(
                    s_script_entries[restored_index].text,
                    s_script_entries[restored_index].text_length)) {
                s_active_checkpoint = *cp;
                return NP2DN_DIALOGUE_RESULT_DONE;
            }
        }
        if ((uint32_t)(GetTickCount() - cp->rollback_start_tick) >
                NP2DN_PC98_JUMP_TIMEOUT_MS &&
            !np2dn_pc98_emulator_paused()) {
            np2jump_disarm();
            s_active_checkpoint = *cp;
            return NP2DN_DIALOGUE_RESULT_ERROR;
        }
        s_active_checkpoint = *cp;
        return NP2DN_DIALOGUE_RESULT_PENDING;
    }
    if ((uint32_t)(GetTickCount() - cp->rollback_start_tick) >
            NP2DN_PC98_JUMP_TIMEOUT_MS &&
        !np2dn_pc98_emulator_paused()) {
        np2jump_disarm();
        s_active_checkpoint = *cp;
        return NP2DN_DIALOGUE_RESULT_ERROR;
    }
    s_active_checkpoint = *cp;
    return NP2DN_DIALOGUE_RESULT_PENDING;
}

static void np2dn_pc98_cancel(
    void *context,
    uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    (void)context;
    (void)checkpoint;
    np2jump_disarm();
    memset(&s_active_checkpoint, 0, sizeof(s_active_checkpoint));
}

static const NP2DN_DialogueAdapter s_adapter = {
    NP2DN_DIALOGUE_CORE_API_VERSION,
    "pc98-ha-script",
    NP2DN_DIALOGUE_CAP_OBSERVE |
        NP2DN_DIALOGUE_CAP_ENUMERATE |
        NP2DN_DIALOGUE_CAP_DIRECT_JUMP |
        NP2DN_DIALOGUE_CAP_VERIFY_STATE |
        NP2DN_DIALOGUE_CAP_VERIFY_UI |
        NP2DN_DIALOGUE_CAP_ROLLBACK |
        NP2DN_DIALOGUE_CAP_VERIFY_CONTINUITY,
    np2dn_pc98_observe,
    np2dn_pc98_enumerate,
    np2dn_pc98_resolve,
    np2dn_pc98_validate,
    np2dn_pc98_prepare,
    np2dn_pc98_apply,
    np2dn_pc98_poll,
    np2dn_pc98_verify,
    np2dn_pc98_rollback,
    np2dn_pc98_cancel
};

int np2dn_pc98_dialogue_adapter_register(void)
{
    static int registered;

    if (!registered) {
        if (!np2dn_dialogue_register_adapter(&s_adapter, NULL)) {
            if (!np2dn_dialogue_select_adapter(s_adapter.adapter_id)) {
                return 0;
            }
        }
        registered = 1;
    }
    return np2dn_dialogue_select_adapter(s_adapter.adapter_id);
}
