#include "np2dn_dialogue_core.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        return 1; \
    } \
} while (0)

typedef struct FakeAdapterContext {
    int fail_apply;
    int apply_calls;
    int poll_calls;
    int verify_calls;
    int rollback_calls;
    int cancel_calls;
} FakeAdapterContext;

static int fake_observe(void *context, NP2DN_DialogueSnapshot *snapshot)
{
    (void)context;
    memset(snapshot, 0, sizeof(*snapshot));
    strcpy(snapshot->dialogue_id, "dialogue_before");
    strcpy(snapshot->scene_id, "scene_alpha");
    snapshot->generation = 10;
    return 1;
}

static int fake_resolve(void *context, const char *dialogue_id,
                        NP2DN_DialogueTarget *target)
{
    (void)context;
    memset(target, 0, sizeof(*target));
    if (dialogue_id == NULL || dialogue_id[0] == '\0') {
        return 0;
    }
    strcpy(target->dialogue_id, dialogue_id);
    strcpy(target->scene_id, "scene_alpha");
    target->locator_kind = 7;
    target->locator_size = 4;
    target->locator[0] = 0x11;
    target->locator[1] = 0x22;
    target->locator[2] = 0x33;
    target->locator[3] = 0x44;
    return 1;
}

static int fake_validate(void *context,
                         const NP2DN_DialogueSnapshot *current,
                         const NP2DN_DialogueTarget *target)
{
    (void)context;
    return current != NULL && target != NULL &&
           strcmp(current->scene_id, target->scene_id) == 0;
}

static int fake_prepare(void *context,
                        const NP2DN_DialogueSnapshot *current,
                        const NP2DN_DialogueTarget *target,
                        uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    (void)context;
    if (current == NULL || target == NULL || checkpoint == NULL) {
        return 0;
    }
    checkpoint[0] = 0xa5;
    return 1;
}

static NP2DN_DialogueResult fake_apply(
    void *opaque, const NP2DN_DialogueTarget *target,
    uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    FakeAdapterContext *context = (FakeAdapterContext *)opaque;
    (void)target;
    (void)checkpoint;
    ++context->apply_calls;
    return context->fail_apply ? NP2DN_DIALOGUE_RESULT_ERROR
                               : NP2DN_DIALOGUE_RESULT_PENDING;
}

static NP2DN_DialogueResult fake_poll(
    void *opaque, const NP2DN_DialogueTarget *target,
    uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    FakeAdapterContext *context = (FakeAdapterContext *)opaque;
    (void)target;
    (void)checkpoint;
    ++context->poll_calls;
    return NP2DN_DIALOGUE_RESULT_DONE;
}

static NP2DN_DialogueResult fake_verify(
    void *opaque, const NP2DN_DialogueTarget *target,
    NP2DN_DialogueSnapshot *observed)
{
    FakeAdapterContext *context = (FakeAdapterContext *)opaque;
    ++context->verify_calls;
    memset(observed, 0, sizeof(*observed));
    if (context->verify_calls == 1) {
        return NP2DN_DIALOGUE_RESULT_PENDING;
    }
    strcpy(observed->dialogue_id, target->dialogue_id);
    strcpy(observed->scene_id, target->scene_id);
    observed->generation = 11;
    return NP2DN_DIALOGUE_RESULT_DONE;
}

static NP2DN_DialogueResult fake_rollback(
    void *opaque,
    uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    FakeAdapterContext *context = (FakeAdapterContext *)opaque;
    (void)checkpoint;
    ++context->rollback_calls;
    return NP2DN_DIALOGUE_RESULT_DONE;
}

static void fake_cancel(
    void *opaque,
    uint8_t checkpoint[NP2DN_DIALOGUE_CHECKPOINT_MAX])
{
    FakeAdapterContext *context = (FakeAdapterContext *)opaque;
    (void)checkpoint;
    ++context->cancel_calls;
}

int main(void)
{
    static const NP2DN_DialogueAdapter adapter = {
        NP2DN_DIALOGUE_CORE_API_VERSION,
        "fake-test-adapter",
        NP2DN_DIALOGUE_CAP_OBSERVE |
        NP2DN_DIALOGUE_CAP_DIRECT_JUMP |
        NP2DN_DIALOGUE_CAP_VERIFY_UI |
        NP2DN_DIALOGUE_CAP_ROLLBACK,
        fake_observe,
        fake_resolve,
        fake_validate,
        fake_prepare,
        fake_apply,
        fake_poll,
        fake_verify,
        fake_rollback,
        fake_cancel
    };
    FakeAdapterContext context;
    NP2DN_DialogueStatus status;

    memset(&context, 0, sizeof(context));
    CHECK(np2dn_dialogue_register_adapter(&adapter, &context),
          "register fake adapter");
    CHECK(np2dn_dialogue_adapter_count() == 1,
          "adapter count");
    CHECK(np2dn_dialogue_select_adapter("fake-test-adapter"),
          "select fake adapter");

    CHECK(np2dn_dialogue_jump_request("dialogue_target"),
          "request target dialogue");
    np2dn_dialogue_tick(); /* observe */
    np2dn_dialogue_tick(); /* resolve */
    np2dn_dialogue_tick(); /* validate */
    np2dn_dialogue_tick(); /* prepare */
    np2dn_dialogue_tick(); /* apply -> pending */
    np2dn_dialogue_tick(); /* poll -> done */
    np2dn_dialogue_tick(); /* verify -> pending */
    np2dn_dialogue_tick(); /* verify -> confirmed */
    np2dn_dialogue_get_status(&status);

    CHECK(status.state == NP2DN_DIALOGUE_STATE_SUCCEEDED,
          "successful transition state");
    CHECK(status.error == NP2DN_DIALOGUE_ERROR_NONE,
          "successful transition error");
    CHECK(strcmp(status.after.dialogue_id, "dialogue_target") == 0,
          "verified destination ID");
    CHECK(context.apply_calls == 1 && context.poll_calls == 1,
          "asynchronous apply/poll sequence");
    CHECK(context.verify_calls == 2,
          "pending presentation verification");

    context.fail_apply = 1;
    context.apply_calls = 0;
    CHECK(np2dn_dialogue_jump_request("dialogue_failure"),
          "request failing transition");
    np2dn_dialogue_tick();
    np2dn_dialogue_tick();
    np2dn_dialogue_tick();
    np2dn_dialogue_tick();
    np2dn_dialogue_tick(); /* apply fails and requests rollback */
    np2dn_dialogue_tick(); /* rollback completes */
    np2dn_dialogue_get_status(&status);

    CHECK(status.state == NP2DN_DIALOGUE_STATE_FAILED,
          "failed transition state");
    CHECK(status.error == NP2DN_DIALOGUE_ERROR_APPLY_FAILED,
          "original apply error retained after rollback");
    CHECK(context.rollback_calls == 1,
          "rollback invoked after apply failure");

    context.fail_apply = 0;
    CHECK(np2dn_dialogue_jump_request("dialogue_cancel"),
          "request cancellable transition");
    np2dn_dialogue_tick();
    np2dn_dialogue_tick();
    np2dn_dialogue_tick();
    np2dn_dialogue_tick(); /* prepared */
    np2dn_dialogue_cancel();
    np2dn_dialogue_get_status(&status);

    CHECK(status.state == NP2DN_DIALOGUE_STATE_CANCELLED,
          "cancelled transition state");
    CHECK(context.cancel_calls == 1,
          "adapter cancellation callback");

    printf("Dialogue core tests passed.\n");
    return 0;
}
