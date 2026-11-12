#include "np2dn_dialogue_core.h"

#include <stddef.h>
#include <string.h>

typedef struct NP2DN_DialogueAdapterSlot {
    NP2DN_DialogueAdapter adapter;
    char adapter_id[NP2DN_DIALOGUE_ID_MAX];
    void *context;
} NP2DN_DialogueAdapterSlot;

static NP2DN_DialogueAdapterSlot g_adapters[NP2DN_DIALOGUE_MAX_ADAPTERS];
static uint32_t g_adapter_count;
static uint32_t g_selected_adapter = 0xffffffffu;
static NP2DN_DialogueStatus g_status;
static NP2DN_DialogueTarget g_target;
static uint8_t g_checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX];
static NP2DN_DialogueCatalogEntry g_catalog[NP2DN_DIALOGUE_CATALOG_MAX];
static NP2DN_DialogueCatalogEntry
    g_catalog_staging[NP2DN_DIALOGUE_CATALOG_MAX];
static uint32_t g_catalog_count;
static uint64_t g_catalog_generation;
static int g_catalog_truncated;
static int g_prepared;
static int g_apply_started;

static int np2dn_dialogue_is_running(NP2DN_DialogueState state)
{
    return (state >= NP2DN_DIALOGUE_STATE_OBSERVING &&
            state <= NP2DN_DIALOGUE_STATE_ROLLING_BACK);
}

static int np2dn_dialogue_string_valid(const char *value, size_t max_length)
{
    size_t i;

    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    for (i = 0; i < max_length; ++i) {
        if (value[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int np2dn_dialogue_string_terminated(const char *value,
                                                  size_t capacity)
{
    size_t i;

    if (value == NULL) {
        return 0;
    }
    for (i = 0; i < capacity; ++i) {
        if (value[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static void np2dn_dialogue_copy_string(char *destination, size_t capacity,
                                       const char *source)
{
    size_t length;

    if (destination == NULL || capacity == 0) {
        return;
    }
    destination[0] = '\0';
    if (source == NULL) {
        return;
    }
    length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
    }
    if (length != 0) {
        memcpy(destination, source, length);
    }
    destination[length] = '\0';
}

static NP2DN_DialogueAdapterSlot *np2dn_dialogue_selected(void)
{
    if (g_selected_adapter >= g_adapter_count) {
        return NULL;
    }
    return &g_adapters[g_selected_adapter];
}

static void np2dn_dialogue_set_error(NP2DN_DialogueError error)
{
    g_status.error = error;
    g_status.state = NP2DN_DIALOGUE_STATE_FAILED;
}

static void np2dn_dialogue_fail(NP2DN_DialogueError error)
{
    NP2DN_DialogueAdapterSlot *slot = np2dn_dialogue_selected();

    g_status.error = error;
    if (g_prepared && slot != NULL &&
        (slot->adapter.capabilities & NP2DN_DIALOGUE_CAP_ROLLBACK) != 0 &&
        slot->adapter.rollback != NULL) {
        g_status.state = NP2DN_DIALOGUE_STATE_ROLLING_BACK;
        return;
    }
    g_status.state = NP2DN_DIALOGUE_STATE_FAILED;
}

int np2dn_dialogue_register_adapter(const NP2DN_DialogueAdapter *adapter,
                                    void *context)
{
    uint32_t i;
    NP2DN_DialogueAdapterSlot *slot;

    if (adapter == NULL ||
        !np2dn_dialogue_string_valid(adapter->adapter_id,
                                     NP2DN_DIALOGUE_ID_MAX) ||
        adapter->api_version != NP2DN_DIALOGUE_CORE_API_VERSION ||
        (adapter->capabilities & NP2DN_DIALOGUE_CAP_OBSERVE) == 0 ||
        (adapter->capabilities & NP2DN_DIALOGUE_CAP_ENUMERATE) == 0 ||
        (adapter->capabilities & NP2DN_DIALOGUE_CAP_DIRECT_JUMP) == 0 ||
        (adapter->capabilities & NP2DN_DIALOGUE_CAP_VERIFY_STATE) == 0 ||
        (adapter->capabilities & NP2DN_DIALOGUE_CAP_VERIFY_UI) == 0 ||
        (adapter->capabilities & NP2DN_DIALOGUE_CAP_VERIFY_CONTINUITY) == 0 ||
        adapter->observe == NULL || adapter->enumerate == NULL ||
        adapter->resolve == NULL ||
        adapter->validate == NULL || adapter->prepare == NULL ||
        adapter->apply == NULL || adapter->poll == NULL ||
        adapter->verify == NULL) {
        return 0;
    }
    if ((adapter->capabilities & NP2DN_DIALOGUE_CAP_ROLLBACK) != 0 &&
        adapter->rollback == NULL) {
        return 0;
    }
    if (np2dn_dialogue_is_running(g_status.state)) {
        return 0;
    }
    for (i = 0; i < g_adapter_count; ++i) {
        if (strcmp(g_adapters[i].adapter.adapter_id,
                   adapter->adapter_id) == 0) {
            return 0;
        }
    }
    if (g_adapter_count >= NP2DN_DIALOGUE_MAX_ADAPTERS) {
        return 0;
    }

    slot = &g_adapters[g_adapter_count];
    memset(slot, 0, sizeof(*slot));
    slot->adapter = *adapter;
    np2dn_dialogue_copy_string(slot->adapter_id, sizeof(slot->adapter_id),
                               adapter->adapter_id);
    slot->adapter.adapter_id = slot->adapter_id;
    slot->context = context;
    ++g_adapter_count;
    return 1;
}

int np2dn_dialogue_select_adapter(const char *adapter_id)
{
    uint32_t i;

    if (!np2dn_dialogue_string_valid(adapter_id, NP2DN_DIALOGUE_ID_MAX) ||
        np2dn_dialogue_is_running(g_status.state)) {
        return 0;
    }
    for (i = 0; i < g_adapter_count; ++i) {
        if (strcmp(g_adapters[i].adapter.adapter_id, adapter_id) == 0) {
            g_selected_adapter = i;
            memset(g_catalog, 0, sizeof(g_catalog));
            g_catalog_count = 0;
            g_catalog_generation = 0;
            g_catalog_truncated = 0;
            g_status.adapter_index = i;
            np2dn_dialogue_copy_string(g_status.adapter_id,
                                       sizeof(g_status.adapter_id),
                                       adapter_id);
            return 1;
        }
    }
    return 0;
}

uint32_t np2dn_dialogue_adapter_count(void)
{
    return g_adapter_count;
}

static int np2dn_dialogue_catalog_entry_valid(
    const NP2DN_DialogueCatalogEntry *entry)
{
    return entry != NULL &&
        np2dn_dialogue_string_valid(entry->dialogue_id,
                                    sizeof(entry->dialogue_id)) &&
        entry->locator_size <= NP2DN_DIALOGUE_LOCATOR_MAX &&
        np2dn_dialogue_string_terminated(entry->scene_id,
                                         sizeof(entry->scene_id)) &&
        np2dn_dialogue_string_terminated(entry->speaker,
                                         sizeof(entry->speaker)) &&
        np2dn_dialogue_string_terminated(entry->text,
                                         sizeof(entry->text));
}

int np2dn_dialogue_catalog_refresh(void)
{
    NP2DN_DialogueAdapterSlot *slot = np2dn_dialogue_selected();
    uint32_t total = 0;
    uint32_t returned = 0;
    uint32_t offset = 0;
    uint32_t expected_total = 0;
    uint64_t generation = 0;
    uint64_t expected_generation = 0;

    if (slot == NULL || np2dn_dialogue_is_running(g_status.state) ||
        (slot->adapter.capabilities & NP2DN_DIALOGUE_CAP_ENUMERATE) == 0 ||
        slot->adapter.enumerate == NULL) {
        return 0;
    }

    memset(g_catalog_staging, 0, sizeof(g_catalog_staging));
    if (slot->adapter.enumerate(
            slot->context, 0, NP2DN_DIALOGUE_CATALOG_PAGE_MAX,
            g_catalog_staging, &returned, &total, &generation) != 1 ||
        returned > NP2DN_DIALOGUE_CATALOG_PAGE_MAX ||
        returned > total) {
        return 0;
    }
    expected_total = total;
    expected_generation = generation;

    while (offset < returned) {
        uint32_t i;
        if (!np2dn_dialogue_catalog_entry_valid(
                &g_catalog_staging[offset])) {
            return 0;
        }
        for (i = 0; i < offset; ++i) {
            if (strcmp(g_catalog_staging[i].dialogue_id,
                       g_catalog_staging[offset].dialogue_id) == 0) {
                return 0;
            }
        }
        ++offset;
    }

    while (offset < total && offset < NP2DN_DIALOGUE_CATALOG_MAX) {
        NP2DN_DialogueCatalogEntry page[NP2DN_DIALOGUE_CATALOG_PAGE_MAX];
        uint32_t capacity = NP2DN_DIALOGUE_CATALOG_MAX - offset;
        uint32_t i;

        if (capacity > NP2DN_DIALOGUE_CATALOG_PAGE_MAX) {
            capacity = NP2DN_DIALOGUE_CATALOG_PAGE_MAX;
        }
        memset(page, 0, sizeof(page));
        returned = 0;
        if (slot->adapter.enumerate(slot->context, offset, capacity,
                                    page, &returned, &total,
                                    &generation) != 1 ||
            returned == 0 || returned > capacity ||
            total != expected_total || generation != expected_generation) {
            return 0;
        }
        for (i = 0; i < returned; ++i) {
            uint32_t j;
            if (!np2dn_dialogue_catalog_entry_valid(&page[i])) {
                return 0;
            }
            for (j = 0; j < offset + i; ++j) {
                const NP2DN_DialogueCatalogEntry *previous =
                    (j < offset) ? &g_catalog_staging[j]
                                 : &page[j - offset];
                if (strcmp(previous->dialogue_id,
                           page[i].dialogue_id) == 0) {
                    return 0;
                }
            }
        }
        memcpy(g_catalog_staging + offset, page,
               returned * sizeof(page[0]));
        offset += returned;
    }

    memcpy(g_catalog, g_catalog_staging,
           offset * sizeof(g_catalog[0]));
    if (offset < NP2DN_DIALOGUE_CATALOG_MAX) {
        memset(g_catalog + offset, 0,
               (NP2DN_DIALOGUE_CATALOG_MAX - offset) *
                   sizeof(g_catalog[0]));
    }
    g_catalog_count = offset;
    g_catalog_generation = expected_generation;
    g_catalog_truncated = (expected_total > offset) ? 1 : 0;
    return 1;
}

uint32_t np2dn_dialogue_catalog_count(void)
{
    return g_catalog_count;
}

uint64_t np2dn_dialogue_catalog_generation(void)
{
    return g_catalog_generation;
}

int np2dn_dialogue_catalog_is_truncated(void)
{
    return g_catalog_truncated;
}

int np2dn_dialogue_catalog_get(uint32_t index,
                              NP2DN_DialogueCatalogEntry *entry)
{
    if (entry == NULL || index >= g_catalog_count) {
        return 0;
    }
    *entry = g_catalog[index];
    return 1;
}

int np2dn_dialogue_jump_request(const char *dialogue_id)
{
    NP2DN_DialogueAdapterSlot *slot = np2dn_dialogue_selected();

    if (np2dn_dialogue_is_running(g_status.state)) {
        g_status.error = NP2DN_DIALOGUE_ERROR_BUSY;
        return 0;
    }
    if (slot == NULL) {
        np2dn_dialogue_set_error(NP2DN_DIALOGUE_ERROR_NO_ADAPTER);
        return 0;
    }
    if (!np2dn_dialogue_string_valid(dialogue_id,
                                     NP2DN_DIALOGUE_ID_MAX)) {
        np2dn_dialogue_set_error(NP2DN_DIALOGUE_ERROR_INVALID_ARGUMENT);
        return 0;
    }

    memset(&g_status.before, 0, sizeof(g_status.before));
    memset(&g_status.after, 0, sizeof(g_status.after));
    memset(&g_target, 0, sizeof(g_target));
    memset(g_checkpoint, 0, sizeof(g_checkpoint));
    g_status.error = NP2DN_DIALOGUE_ERROR_NONE;
    g_status.tick_count = 0;
    g_status.rollback_available =
        (slot->adapter.capabilities & NP2DN_DIALOGUE_CAP_ROLLBACK) ? 1 : 0;
    np2dn_dialogue_copy_string(g_status.requested_dialogue_id,
                               sizeof(g_status.requested_dialogue_id),
                               dialogue_id);
    g_status.resolved_dialogue_id[0] = '\0';
    g_prepared = 0;
    g_apply_started = 0;
    g_status.state = NP2DN_DIALOGUE_STATE_OBSERVING;
    return 1;
}

void np2dn_dialogue_tick(void)
{
    NP2DN_DialogueAdapterSlot *slot = np2dn_dialogue_selected();
    NP2DN_DialogueResult result;

    if (!np2dn_dialogue_is_running(g_status.state)) {
        return;
    }
    if (slot == NULL) {
        np2dn_dialogue_fail(NP2DN_DIALOGUE_ERROR_NO_ADAPTER);
        return;
    }
    ++g_status.tick_count;

    switch (g_status.state) {
    case NP2DN_DIALOGUE_STATE_OBSERVING:
        memset(&g_status.before, 0, sizeof(g_status.before));
        if (slot->adapter.observe(slot->context, &g_status.before) != 1) {
            np2dn_dialogue_fail(NP2DN_DIALOGUE_ERROR_OBSERVE_FAILED);
            return;
        }
        g_status.state = NP2DN_DIALOGUE_STATE_RESOLVING;
        break;

    case NP2DN_DIALOGUE_STATE_RESOLVING:
        memset(&g_target, 0, sizeof(g_target));
        if (slot->adapter.resolve(slot->context,
                                  g_status.requested_dialogue_id,
                                  &g_target) != 1 ||
            !np2dn_dialogue_string_valid(g_target.dialogue_id,
                                         sizeof(g_target.dialogue_id)) ||
            g_target.locator_size > NP2DN_DIALOGUE_LOCATOR_MAX) {
            np2dn_dialogue_fail(NP2DN_DIALOGUE_ERROR_TARGET_NOT_FOUND);
            return;
        }
        np2dn_dialogue_copy_string(g_status.resolved_dialogue_id,
                                   sizeof(g_status.resolved_dialogue_id),
                                   g_target.dialogue_id);
        g_status.state = NP2DN_DIALOGUE_STATE_VALIDATING;
        break;

    case NP2DN_DIALOGUE_STATE_VALIDATING:
        if (slot->adapter.validate(slot->context, &g_status.before,
                                   &g_target) != 1) {
            np2dn_dialogue_fail(NP2DN_DIALOGUE_ERROR_VALIDATION_FAILED);
            return;
        }
        g_status.state = NP2DN_DIALOGUE_STATE_PREPARING;
        break;

    case NP2DN_DIALOGUE_STATE_PREPARING:
        if (slot->adapter.prepare(slot->context, &g_status.before,
                                  &g_target, g_checkpoint) != 1) {
            np2dn_dialogue_fail(NP2DN_DIALOGUE_ERROR_PREPARE_FAILED);
            return;
        }
        g_prepared = 1;
        g_status.state = NP2DN_DIALOGUE_STATE_APPLYING;
        break;

    case NP2DN_DIALOGUE_STATE_APPLYING:
        if (!g_apply_started) {
            result = slot->adapter.apply(slot->context, &g_target,
                                         g_checkpoint);
            if (result == NP2DN_DIALOGUE_RESULT_PENDING) {
                g_apply_started = 1;
                break;
            }
        }
        else {
            result = slot->adapter.poll(slot->context, &g_target,
                                        g_checkpoint);
        }
        if (result == NP2DN_DIALOGUE_RESULT_PENDING) {
            break;
        }
        if (result != NP2DN_DIALOGUE_RESULT_DONE) {
            np2dn_dialogue_fail(NP2DN_DIALOGUE_ERROR_APPLY_FAILED);
            return;
        }
        g_status.state = NP2DN_DIALOGUE_STATE_VERIFYING;
        break;

    case NP2DN_DIALOGUE_STATE_VERIFYING:
        memset(&g_status.after, 0, sizeof(g_status.after));
        result = slot->adapter.verify(slot->context, &g_target,
                                      &g_status.after);
        if (result == NP2DN_DIALOGUE_RESULT_PENDING) {
            break;
        }
        if (result != NP2DN_DIALOGUE_RESULT_DONE ||
            strcmp(g_status.after.dialogue_id, g_target.dialogue_id) != 0 ||
            (g_status.after.flags &
             NP2DN_DIALOGUE_SNAPSHOT_STATE_VERIFIED) == 0 ||
            (g_status.after.flags &
             NP2DN_DIALOGUE_SNAPSHOT_PRESENTED) == 0) {
            np2dn_dialogue_fail(NP2DN_DIALOGUE_ERROR_VERIFY_FAILED);
            return;
        }
        if ((g_status.after.flags &
             NP2DN_DIALOGUE_SNAPSHOT_CONTINUATION_READY) == 0) {
            np2dn_dialogue_fail(NP2DN_DIALOGUE_ERROR_CONTINUITY_FAILED);
            return;
        }
        g_status.state = NP2DN_DIALOGUE_STATE_SUCCEEDED;
        g_status.error = NP2DN_DIALOGUE_ERROR_NONE;
        break;

    case NP2DN_DIALOGUE_STATE_ROLLING_BACK:
        result = slot->adapter.rollback(slot->context, g_checkpoint);
        if (result == NP2DN_DIALOGUE_RESULT_PENDING) {
            break;
        }
        if (result != NP2DN_DIALOGUE_RESULT_DONE) {
            g_status.error = NP2DN_DIALOGUE_ERROR_ROLLBACK_FAILED;
        }
        g_status.state = NP2DN_DIALOGUE_STATE_FAILED;
        break;

    default:
        np2dn_dialogue_fail(NP2DN_DIALOGUE_ERROR_ADAPTER_CONTRACT);
        break;
    }
}

void np2dn_dialogue_cancel(void)
{
    NP2DN_DialogueAdapterSlot *slot = np2dn_dialogue_selected();

    if (!np2dn_dialogue_is_running(g_status.state)) {
        return;
    }
    if (slot != NULL && slot->adapter.cancel != NULL) {
        slot->adapter.cancel(slot->context, g_checkpoint);
    }
    g_status.state = NP2DN_DIALOGUE_STATE_CANCELLED;
    g_status.error = NP2DN_DIALOGUE_ERROR_NONE;
}

void np2dn_dialogue_get_status(NP2DN_DialogueStatus *status)
{
    if (status != NULL) {
        *status = g_status;
    }
}
